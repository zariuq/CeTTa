#include "library_tensor.h"
#include "library.h"
#include "tensor.h"
#include "tensor_screening.h"
#include "petta_semantics.h"
#include "eval.h"
#include "prepared_pure_machine.h"
#include "lang.h"
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#if CETTA_BUILD_WITH_JSON_GSLT
#include "native/json_direct_v1.h"
#include "native/decimal_value_v1.h"
#endif

typedef struct {
    CettaTensorDType dtype;
    CettaTensorDuplicates duplicates;
    size_t *shape, rank, logical, length, capacity;
    size_t *coordinates;
    unsigned char *values;
    bool frozen;
} TensorBuilder;
static const char *const builder_kind="tensor-builder-v2";
typedef struct {
    CettaTensor *source, *normalized;
    CettaTensorScreenIndex *screen;
} TensorCosineIndex;
static const char *const cosine_kind="tensor-cosine-index-v2";
static void cosine_release(void *raw) {
    TensorCosineIndex *index=raw;
    cetta_tensor_screen_index_release(index->screen);
    cetta_tensor_release(index->source); cetta_tensor_release(index->normalized); free(index);
}

static size_t retained_add(size_t a, size_t b) { return a > SIZE_MAX - b ? SIZE_MAX : a + b; }
static size_t cosine_retained_bound(const TensorCosineIndex *index) {
    size_t bytes = retained_add(sizeof(*index), cetta_tensor_retained_bytes(index->source));
    bytes = retained_add(bytes, cetta_tensor_retained_bytes(index->normalized));
    return retained_add(bytes, cetta_tensor_screen_index_retained_bound(index->screen));
}

static CettaTensorStatus cosine_retrieve(TensorCosineIndex *index, CettaTensor *query,
    CettaTensor *mask, size_t k, CettaTensor **indices, CettaTensor **scores) {
    *indices = NULL; *scores = NULL;
    if (index->screen) {
        CettaTensorScreenReceipt *receipt = NULL;
        CettaTensorStatus status = cetta_tensor_screen(index->screen, query, mask, k, &receipt);
        if (status == CETTA_TENSOR_OK) status = cetta_tensor_screen_rerank(receipt, indices, scores);
        cetta_tensor_screen_receipt_release(receipt);
        return status;
    }
    CettaTensor *all = NULL;
    CettaTensorStatus status = cetta_tensor_matvec(index->normalized, query, CETTA_TENSOR_SEQUENTIAL, &all);
    if (status == CETTA_TENSOR_OK) status = cetta_tensor_top_k(all, mask, k, indices);
    if (status == CETTA_TENSOR_OK) status = cetta_tensor_gather(all, *indices, scores);
    cetta_tensor_release(all);
    if (status != CETTA_TENSOR_OK) {
        cetta_tensor_release(*indices); cetta_tensor_release(*scores);
        *indices = NULL; *scores = NULL;
    }
    return status;
}

static Atom *fault(Arena *a,CettaTensorStatus status) {
    return atom_expr2(a,atom_symbol(a,"Tensor:Fault"),atom_symbol(a,cetta_tensor_status_name(status)));
}
static Atom *state_fault(Arena *a,const char *state) {
    return atom_expr2(a,atom_symbol(a,"Tensor:Fault"),atom_symbol(a,state));
}
static Atom *publish(Arena *a,CettaTensorStatus status,CettaTensor *tensor) {
    if(status!=CETTA_TENSOR_OK) { cetta_tensor_release(tensor); return fault(a,status); }
    Atom *value=cetta_tensor_atom(a,tensor); cetta_tensor_release(tensor);
    return value ? value : fault(a,CETTA_TENSOR_NOMEM);
}
static CettaTensorDType dtype(Atom *a) {
#define DTYPE(name, code, text, width, family, precision) if(atom_is_symbol(a,text)) return CETTA_TENSOR_##name;
    CETTA_TENSOR_DTYPE_ROWS(DTYPE)
#undef DTYPE
    return 0;
}
static bool unsigned_value(Atom *a,uint64_t *out) {
    if(!a || a->kind!=ATOM_GROUNDED) return false;
    if(a->ground.gkind==GV_INT && a->ground.ival>=0) { *out=(uint64_t)a->ground.ival; return true; }
    if(a->ground.gkind!=GV_BIGINT) return false;
    const char *text=atom_bigint_cstr(a);
    if(!text || !*text) return false;
    for(const char *p=text;*p;p++) if(*p<'0' || *p>'9') return false;
    errno=0; char *end=NULL; unsigned long long n=strtoull(text,&end,10);
    if(errno || !end || *end) return false;
    *out=(uint64_t)n; return true;
}
static bool natural(Atom *a,size_t *out) {
    uint64_t n; if(!unsigned_value(a,&n) || n>SIZE_MAX) return false; *out=(size_t)n; return true;
}
static bool sequence(Arena *a,Atom *value,Atom *const **items,CettaExprLen *length) {
    if(petta_semantics_is_open_cons_value(value)) value=petta_semantics_closed_list(a,value);
    return atom_sequence_view(value,items,length);
}
static CettaTensorStatus shape(Arena *a,Atom *value,size_t **out,size_t *rank) {
    Atom *const *items; CettaExprLen n;
    *out=NULL;
    if(!sequence(a,value,&items,&n)) return CETTA_TENSOR_SHAPE;
    if(n>SIZE_MAX/sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    size_t *dimensions=n ? malloc((size_t)n*sizeof(size_t)) : NULL;
    if(n && !dimensions) return CETTA_TENSOR_NOMEM;
    for(size_t i=0;i<n;i++) if(!natural(items[i],&dimensions[i])) { free(dimensions); return CETTA_TENSOR_SHAPE; }
    *out=dimensions; *rank=(size_t)n; return CETTA_TENSOR_OK;
}
static CettaTensorStatus scalar(CettaTensorDType d,Atom *a,CettaTensorScalar *out) {
    memset(out,0,sizeof(*out));
    if(d==CETTA_TENSOR_BOOL) {
        if(a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_BOOL) { out->u8=a->ground.bval; return CETTA_TENSOR_OK; }
        if(atom_is_symbol(a,"true") || atom_is_symbol(a,"True")) { out->u8=1; return CETTA_TENSOR_OK; }
        if(atom_is_symbol(a,"false") || atom_is_symbol(a,"False")) return CETTA_TENSOR_OK;
        return CETTA_TENSOR_VALUE;
    }
    if(d==CETTA_TENSOR_INDEX) return unsigned_value(a,&out->index) ? CETTA_TENSOR_OK : CETTA_TENSOR_VALUE;
    if(!a || a->kind!=ATOM_GROUNDED) return CETTA_TENSOR_VALUE;
    if(d==CETTA_TENSOR_I64 || d==CETTA_TENSOR_U8) {
        if(a->ground.gkind!=GV_INT) return CETTA_TENSOR_VALUE;
        if(d==CETTA_TENSOR_U8) {
            if(a->ground.ival<0 || a->ground.ival>UINT8_MAX) return CETTA_TENSOR_OVERFLOW;
            out->u8=(uint8_t)a->ground.ival;
        } else out->i64=a->ground.ival;
        return CETTA_TENSOR_OK;
    }
    double value;
    if(a->ground.gkind==GV_FLOAT) value=a->ground.fval;
    else if(a->ground.gkind==GV_INT) value=(double)a->ground.ival;
    else return CETTA_TENSOR_VALUE;
    if(d==CETTA_TENSOR_F64) out->f64=value;
    else if(d==CETTA_TENSOR_F32) {
        if(isfinite(value) && fabs(value)>FLT_MAX) return CETTA_TENSOR_OVERFLOW;
        out->f32=(float)value;
    } else return CETTA_TENSOR_DTYPE;
    return CETTA_TENSOR_OK;
}
static Atom *scalar_atom(Arena *a,CettaTensorDType d,CettaTensorScalar value) {
    switch(d) {
    case CETTA_TENSOR_I64: return atom_int(a,value.i64);
    case CETTA_TENSOR_F64: return atom_float(a,value.f64);
    case CETTA_TENSOR_F32: return atom_float(a,value.f32);
    case CETTA_TENSOR_U8: return atom_int(a,value.u8);
    case CETTA_TENSOR_BOOL:
        return eval_current_language_id()==CETTA_LANGUAGE_PETTA
            ? atom_symbol(a,value.u8 ? "true" : "false") : atom_bool(a,value.u8!=0);
    case CETTA_TENSOR_INDEX: {
        if(value.index<=INT64_MAX) return atom_int(a,(int64_t)value.index);
        char text[32]; snprintf(text,sizeof(text),"%" PRIu64,value.index); return atom_bigint(a,text);
    }
    }
    return NULL;
}
static bool coordinate(Arena *a,Atom *value,const size_t *dimensions,size_t rank,size_t count,size_t *flat) {
    if(natural(value,flat)) return *flat<count;
    Atom *const *items; CettaExprLen n;
    if(!sequence(a,value,&items,&n) || n!=rank) return false;
    size_t result=0;
    for(size_t i=0;i<rank;i++) {
        size_t x; if(!natural(items[i],&x) || x>=dimensions[i]) return false;
        result=result*dimensions[i]+x;
    }
    if(result>=count) return false;
    *flat=result; return true;
}
static bool duplicate_policy(Atom *value,CettaTensorDuplicates *out) {
    if(atom_is_symbol(value,"reject")) *out=CETTA_TENSOR_REJECT;
    else if(atom_is_symbol(value,"first")) *out=CETTA_TENSOR_FIRST;
    else if(atom_is_symbol(value,"last")) *out=CETTA_TENSOR_LAST;
    else if(atom_is_symbol(value,"sum")) *out=CETTA_TENSOR_SUM;
    else return false;
    return true;
}
static void builder_release(void *raw) {
    TensorBuilder *b=raw;
    free(b->shape); free(b->coordinates); free(b->values); free(b);
}
static Atom *builder_new(CettaLibraryContext *context,Arena *a,Atom **args) {
    TensorBuilder *b=calloc(1,sizeof(*b)); if(!b) return fault(a,CETTA_TENSOR_NOMEM);
    b->dtype=dtype(args[0]);
    CettaTensorStatus s=shape(a,args[1],&b->shape,&b->rank);
    if(s==CETTA_TENSOR_OK) s=cetta_tensor_coordinate_count(b->shape,b->rank,&b->logical);
    if(s==CETTA_TENSOR_OK && !duplicate_policy(args[2],&b->duplicates)) s=CETTA_TENSOR_VALUE;
    if(s!=CETTA_TENSOR_OK) { builder_release(b); return fault(a,s); }
    uint64_t id;
    if(!cetta_native_handle_alloc(context,builder_kind,b,builder_release,&id)) { builder_release(b); return fault(a,CETTA_TENSOR_NOMEM); }
    Atom *handle=cetta_native_handle_owned_atom(context,a,builder_kind,id);
    if(!handle) { (void)cetta_native_handle_close(context,builder_kind,id); return fault(a,CETTA_TENSOR_NOMEM); }
    return handle;
}
static Atom *builder_put(Arena *a,TensorBuilder *b,Atom **args) {
    if(b->frozen) return state_fault(a,"frozen-builder");
    size_t flat; CettaTensorScalar value;
    if(!coordinate(a,args[1],b->shape,b->rank,b->logical,&flat)) return fault(a,CETTA_TENSOR_INDEX_ERROR);
    CettaTensorStatus s=scalar(b->dtype,args[2],&value); if(s!=CETTA_TENSOR_OK) return fault(a,s);
    size_t width=cetta_tensor_itemsize(b->dtype);
    if(b->length==b->capacity) {
        size_t capacity=b->capacity ? b->capacity*2 : 16;
        if(capacity<b->capacity || capacity>SIZE_MAX/sizeof(size_t) || capacity>SIZE_MAX/width) return fault(a,CETTA_TENSOR_OVERFLOW);
        size_t *coordinates=malloc(capacity*sizeof(size_t)); unsigned char *values=malloc(capacity*width);
        if(!coordinates || !values) { free(coordinates); free(values); return fault(a,CETTA_TENSOR_NOMEM); }
        if(b->length) { memcpy(coordinates,b->coordinates,b->length*sizeof(size_t)); memcpy(values,b->values,b->length*width); }
        free(b->coordinates); free(b->values); b->coordinates=coordinates; b->values=values; b->capacity=capacity;
    }
    b->coordinates[b->length]=flat; memcpy(b->values+b->length*width,&value,width); b->length++;
    return atom_unit(a);
}
static Atom *construct(Arena *a,Atom **args,bool coo) {
    CettaTensorDType d=dtype(args[0]); size_t *dimensions=NULL,rank=0,count=0,bytes=0;
    CettaTensorStatus s=shape(a,args[1],&dimensions,&rank);
    if(s==CETTA_TENSOR_OK) s=coo ? cetta_tensor_coordinate_count(dimensions,rank,&count) :
        cetta_tensor_byte_count(d,dimensions,rank,&count,&bytes);
    Atom *const *items=NULL; CettaExprLen n=0;
    if(s==CETTA_TENSOR_OK && !sequence(a,args[2],&items,&n)) s=CETTA_TENSOR_VALUE;
    size_t width=cetta_tensor_itemsize(d);
    if(s==CETTA_TENSOR_OK && (!coo && n!=count)) s=CETTA_TENSOR_COUNT;
    if(s==CETTA_TENSOR_OK && (n>SIZE_MAX/width || n>SIZE_MAX/sizeof(size_t))) s=CETTA_TENSOR_OVERFLOW;
    unsigned char *values=s==CETTA_TENSOR_OK && n ? malloc((size_t)n*width) : NULL;
    size_t *coordinates=s==CETTA_TENSOR_OK && coo && n ? malloc((size_t)n*sizeof(size_t)) : NULL;
    if(s==CETTA_TENSOR_OK && n && (!values || (coo && !coordinates))) s=CETTA_TENSOR_NOMEM;
    for(size_t i=0;s==CETTA_TENSOR_OK && i<n;i++) {
        Atom *value=items[i]; CettaTensorScalar x;
        if(coo) {
            Atom *const *pair; CettaExprLen length;
            if(!sequence(a,value,&pair,&length) || length!=2) { s=CETTA_TENSOR_VALUE; break; }
            if(!coordinate(a,pair[0],dimensions,rank,count,&coordinates[i])) { s=CETTA_TENSOR_INDEX_ERROR; break; }
            value=pair[1];
        }
        s=scalar(d,value,&x); if(s==CETTA_TENSOR_OK) memcpy(values+i*width,&x,width);
    }
    CettaTensor *tensor=NULL; CettaTensorDuplicates duplicates=CETTA_TENSOR_REJECT;
    if(coo && !duplicate_policy(args[3],&duplicates)) s=CETTA_TENSOR_VALUE;
    if(s==CETTA_TENSOR_OK) {
        s=coo ? cetta_tensor_coo(d,dimensions,rank,coordinates,values,(size_t)n,duplicates,&tensor) :
            cetta_tensor_new_take(d,dimensions,rank,values,bytes,&tensor);
        if(!coo && s==CETTA_TENSOR_OK) values=NULL;
    }
    free(dimensions); free(values); free(coordinates); return publish(a,s,tensor);
}
#if CETTA_BUILD_WITH_JSON_GSLT
typedef struct {
    unsigned char *values;
    size_t count,seen,width,capacity,bytes;
    CettaTensorDType dtype;
    char small[128],*text;
    bool allocation_failed;
} TensorNumberSink;
static bool number_sink(void *raw,const uint8_t *bytes,size_t length,size_t ordinal) {
    TensorNumberSink *sink=raw;
    if(ordinal>=sink->count || length==SIZE_MAX || eval_native_interrupted()) return false;
    if(!sink->values && sink->bytes) {
        sink->values=malloc(sink->bytes);
        if(!sink->values) { sink->allocation_failed=true; return false; }
    }
    if(sink->dtype==CETTA_TENSOR_BOOL) {
        sink->values[ordinal]=length==4 && !memcmp(bytes,"true",4);
        sink->seen++; return true;
    }
    if(length+1>sink->capacity) {
        char *larger=malloc(length+1); if(!larger) { sink->allocation_failed=true; return false; }
        if(sink->text!=sink->small) free(sink->text);
        sink->text=larger; sink->capacity=length+1;
    }
    memcpy(sink->text,bytes,length); sink->text[length]='\0';
    CettaTensorScalar value={0}; errno=0; char *end=NULL;
    if(sink->dtype==CETTA_TENSOR_F64 || sink->dtype==CETTA_TENSOR_F32) {
        double x;
        if(!cetta_decimal_nearest_validated_v1(sink->text,&x) || !isfinite(x)) return false;
        if(sink->dtype==CETTA_TENSOR_F64) value.f64=x;
        else { if(fabs(x)>FLT_MAX) return false; value.f32=(float)x; }
    } else if(sink->dtype==CETTA_TENSOR_I64) {
        long long x=strtoll(sink->text,&end,10);
        if(errno || end==sink->text || *end || x<INT64_MIN || x>INT64_MAX) return false;
        value.i64=(int64_t)x;
    } else {
        unsigned long long x=strtoull(sink->text,&end,10);
        if(errno || end==sink->text || *end || (sink->text[0]=='-' && x!=0)) return false;
        if(sink->dtype==CETTA_TENSOR_INDEX) value.index=(uint64_t)x;
        else if(sink->dtype==CETTA_TENSOR_U8 && x<=UINT8_MAX) value.u8=(uint8_t)x;
        else return false;
    }
    memcpy(sink->values+ordinal*sink->width,&value,sink->width); sink->seen++; return true;
}
static Atom *from_json(Arena *a,Atom **args) {
    CettaTensorDType d=dtype(args[0]); size_t *dimensions=NULL,rank=0,count=0,bytes=0;
    CettaTensorStatus s=shape(a,args[1],&dimensions,&rank);
    if(s==CETTA_TENSOR_OK) s=cetta_tensor_byte_count(d,dimensions,rank,&count,&bytes);
    if(s!=CETTA_TENSOR_OK) { free(dimensions); return fault(a,s); }
    if(!args[2] || args[2]->kind!=ATOM_GROUNDED || args[2]->ground.gkind!=GV_STRING) { free(dimensions); return fault(a,CETTA_TENSOR_VALUE); }
    Atom *const *path_items; CettaExprLen path_length;
    if(!sequence(a,args[3],&path_items,&path_length)) { free(dimensions); return fault(a,CETTA_TENSOR_VALUE); }
    Atom *path=atom_expr(a,(Atom **)path_items,path_length);
    TensorNumberSink sink={.count=count,.width=cetta_tensor_itemsize(d),.dtype=d,.bytes=bytes};
    sink.text=sink.small; sink.capacity=sizeof(sink.small);
    bool found=false; char message[256]={0}; CettaJsonRuntimeV1Status status;
    bool ok=cetta_json_direct_v1_scalar_array((const uint8_t *)args[2]->ground.sval,atom_string_len(args[2]),
        path,dimensions,rank,d==CETTA_TENSOR_BOOL ? CETTA_JSON_SCALAR_BOOLEAN_V1 : CETTA_JSON_SCALAR_NUMBER_V1,
        0,number_sink,&sink,&found,&status,message,sizeof(message));
    if(sink.text!=sink.small) free(sink.text);
    unsigned char *values=sink.values;
    CettaTensor *tensor=NULL; Atom *answer=NULL;
    if(ok && !found) answer=atom_symbol(a,"JsonMissingV1");
    else if(sink.allocation_failed || status==CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE) answer=fault(a,CETTA_TENSOR_NOMEM);
    else if(!ok || sink.seen!=count) answer=atom_expr3(a,atom_symbol(a,"Tensor:Fault"),atom_symbol(a,"typed-json"),atom_string(a,message));
    else {
        s=cetta_tensor_new_take(d,dimensions,rank,values,bytes,&tensor);
        if(s==CETTA_TENSOR_OK) values=NULL;
        answer=publish(a,s,tensor);
    }
    free(values); free(dimensions); return answer;
}
#endif
/* Typed scalar buffers can become sparse values without expanding an entry
 * per number into syntax. Coordinate and value buffers must have complete
 * vector support; sparse absence is never mistaken for a stored zero. */
static Atom *from_coordinates(Arena *a,Atom *shape_value,CettaTensor *indices,
    CettaTensor *values,Atom *policy) {
    size_t *dimensions=NULL,rank=0,count=0,n=cetta_tensor_count(indices);
    CettaTensorDType d=cetta_tensor_dtype(values);
    CettaTensorStatus s=shape(a,shape_value,&dimensions,&rank);
    if(s==CETTA_TENSOR_OK && cetta_tensor_dtype(indices)!=CETTA_TENSOR_INDEX) s=CETTA_TENSOR_DTYPE;
    if(s==CETTA_TENSOR_OK && (cetta_tensor_rank(indices)!=1 || cetta_tensor_rank(values)!=1 ||
        cetta_tensor_count(values)!=n || cetta_tensor_stored(indices)!=n || cetta_tensor_stored(values)!=n)) s=CETTA_TENSOR_SHAPE;
    if(s==CETTA_TENSOR_OK) s=cetta_tensor_coordinate_count(dimensions,rank,&count);
    size_t width=cetta_tensor_itemsize(d);
    if(s==CETTA_TENSOR_OK && (n>SIZE_MAX/sizeof(size_t) || n>SIZE_MAX/width)) s=CETTA_TENSOR_OVERFLOW;
    size_t *coordinates=s==CETTA_TENSOR_OK && n ? malloc(n*sizeof(*coordinates)) : NULL;
    unsigned char *data=s==CETTA_TENSOR_OK && n ? malloc(n*width) : NULL;
    if(s==CETTA_TENSOR_OK && n && (!coordinates || !data)) s=CETTA_TENSOR_NOMEM;
    if(s==CETTA_TENSOR_OK && cetta_tensor_layout(values)==CETTA_TENSOR_DENSE)
        s=cetta_tensor_copy_values(values,data,n*width);
    for(size_t i=0;s==CETTA_TENSOR_OK && i<n;i++) {
        if((i&1023u)==0u && eval_native_interrupted()) break;
        bool present=false; CettaTensorScalar x;
        s=cetta_tensor_get(indices,i,&present,&x);
        if(s==CETTA_TENSOR_OK && (!present || x.index>SIZE_MAX || x.index>=count)) s=CETTA_TENSOR_INDEX_ERROR;
        if(s!=CETTA_TENSOR_OK) break;
        coordinates[i]=(size_t)x.index;
        if(cetta_tensor_layout(values)!=CETTA_TENSOR_DENSE) {
            s=cetta_tensor_get(values,i,&present,&x);
            if(s==CETTA_TENSOR_OK && !present) s=CETTA_TENSOR_SHAPE;
            if(s==CETTA_TENSOR_OK) memcpy(data+i*width,&x,width);
        }
    }
    CettaTensorDuplicates duplicates=CETTA_TENSOR_REJECT;
    if(s==CETTA_TENSOR_OK && !duplicate_policy(policy,&duplicates)) s=CETTA_TENSOR_VALUE;
    CettaTensor *tensor=NULL;
    if(s==CETTA_TENSOR_OK && !eval_native_interrupted())
        s=cetta_tensor_coo(d,dimensions,rank,coordinates,data,n,duplicates,&tensor);
    Atom *answer=NULL;
    if(eval_native_interrupted()) cetta_tensor_release(tensor);
    else answer=publish(a,s,tensor);
    free(dimensions); free(coordinates); free(data); return answer;
}
static bool tensor_callback_stop(void *context) {
    (void)context; return eval_native_interrupted();
}

static bool zip_contract_supported(void) {
#define OBSERVATION(name, policy) \
    if (!strcmp(name, "zipmap")) return !strcmp(policy, \
        "EqualShapeSupport|CapturedValueCall|CoordinateAscending|AtomicFallback|COOResult");
    CETTA_TENSOR_OBSERVATION_ROWS(OBSERVATION)
#undef OBSERVATION
    return false;
}

static bool callback_encoding(CettaTensorDType dtype, CettaTensorCallbackEncoding *encoding) {
    switch (dtype) {
#define CALLBACK_INPUT(name, representation) \
    case CETTA_TENSOR_##name: *encoding = representation; return true;
    CETTA_TENSOR_CALLBACK_INPUT_ROWS(CALLBACK_INPUT)
#undef CALLBACK_INPUT
    default: return false;
    }
}

static Atom *callback_atom(Arena *arena, CettaTensorCallbackEncoding encoding,
                          CettaTensorScalar value, Atom *leaf) {
    switch (encoding) {
    case CETTA_TENSOR_CALLBACK_SIGNED64ATOM:
        atom_scalar_leaf_int(leaf, value.i64); break;
    case CETTA_TENSOR_CALLBACK_UNSIGNED8ATOM:
        atom_scalar_leaf_int(leaf, value.u8); break;
    case CETTA_TENSOR_CALLBACK_FLOAT32ATOM:
        atom_scalar_leaf_float(leaf, value.f32); break;
    case CETTA_TENSOR_CALLBACK_FLOAT64ATOM:
        atom_scalar_leaf_float(leaf, value.f64); break;
    case CETTA_TENSOR_CALLBACK_UNSIGNED64ATOM:
        if (value.index > INT64_MAX)
            return scalar_atom(arena, CETTA_TENSOR_INDEX, value);
        atom_scalar_leaf_int(leaf, (int64_t)value.index); break;
    default: return NULL;
    }
    return leaf;
}

/* Only unpublished scratch is used by the prepared callback. A declined
 * callback restarts the complete authored map; it is never an empty answer. */
static Atom *zip_map(Space *space, Arena *destination, Atom **args) {
    Atom *const *inputs, *const *captures;
    CettaExprLen count, captured;
    if (!zip_contract_supported() || !sequence(destination, args[1], &inputs, &count) || !count ||
        !sequence(destination, args[3], &captures, &captured) ||
        count > 64u || captured > 64u - count ||
        dtype(args[0]) != CETTA_TENSOR_F64 || !args[2])
        return NULL;
    CettaTensor *tensors[64];
    CettaTensorCallbackEncoding encodings[64];
    for (size_t i = 0; i < captured; i++)
        if (!captures[i] || captures[i]->kind != ATOM_GROUNDED ||
            (captures[i]->ground.gkind != GV_INT && captures[i]->ground.gkind != GV_FLOAT))
            return NULL;
    for (size_t i = 0; i < count; i++) {
        tensors[i] = cetta_tensor_from_atom(inputs[i]);
        if (!tensors[i]) return NULL;
        if (!callback_encoding(cetta_tensor_dtype(tensors[i]), &encodings[i]))
            return NULL;
        if (i && (cetta_tensor_rank(tensors[i]) != cetta_tensor_rank(tensors[0]) ||
            cetta_tensor_stored(tensors[i]) != cetta_tensor_stored(tensors[0]) ||
            (cetta_tensor_rank(tensors[0]) &&
             memcmp(cetta_tensor_shape(tensors[i]), cetta_tensor_shape(tensors[0]),
                    cetta_tensor_rank(tensors[0]) * sizeof(size_t)))))
            return NULL;
    }
    const char *reference = getenv("CETTA_TENSOR_MAP_REFERENCE");
    if (reference && *reference && strcmp(reference, "0")) return NULL;
    Arena source, scratch;
    arena_init(&source); arena_set_hashcons(&source, NULL);
    arena_init(&scratch); arena_set_hashcons(&scratch, NULL);
    /* Keep the nursery's first block across row resets. */
    (void)arena_alloc(&scratch, 1);
    ArenaMark mark = arena_mark(&scratch);
    Atom leaves[64], *call_args[65];
    call_args[0] = args[2];
    for (size_t i = 0; i < captured; i++) call_args[i + 1] = captures[i];
    for (size_t i = 0; i < count; i++) {
        atom_scalar_leaf_float(&leaves[i], 1.0);
        call_args[captured + i + 1] = &leaves[i];
    }
    Atom *prototype = atom_expr(&source, call_args, count + captured + 1);
    bool lexical = args[2]->kind != ATOM_SYMBOL;
    CettaPreparedPureProgram *program = lexical
        ? eval_prepare_pure_lexical_values(
            space, &source, args[2], (uint32_t)(count + captured))
        : eval_prepare_pure_value_call(space, &source, prototype);
    Atom *answer = NULL;
    CettaTensor *coordinates = NULL, *unused = NULL, *result = NULL;
    size_t n = cetta_tensor_stored(tensors[0]);
    double *values = NULL;
    size_t *keys = NULL;
    if (!program || n > SIZE_MAX / sizeof(double) || n > SIZE_MAX / sizeof(size_t))
        goto cleanup;
    if (cetta_tensor_layout(tensors[0]) != CETTA_TENSOR_DENSE &&
        cetta_tensor_entries(tensors[0], &coordinates, &unused) != CETTA_TENSOR_OK)
        goto cleanup;
    values = n ? malloc(n * sizeof(*values)) : NULL;
    keys = n ? malloc(n * sizeof(*keys)) : NULL;
    if (n && (!values || !keys)) goto cleanup;
    for (size_t row = 0; row < n; row++) {
        if ((row & 255u) == 0u && eval_native_interrupted()) goto cleanup;
        size_t coordinate = row;
        bool present;
        CettaTensorScalar scalar_value;
        if (coordinates) {
            if (cetta_tensor_get(coordinates, row, &present, &scalar_value) != CETTA_TENSOR_OK || !present)
                goto cleanup;
            coordinate = (size_t)scalar_value.index;
        }
        keys[row] = coordinate;
        for (size_t i = 0; i < count; i++) {
            if (cetta_tensor_stored(tensors[i]) != n ||
                cetta_tensor_get(tensors[i], coordinate, &present, &scalar_value) != CETTA_TENSOR_OK || !present)
                goto cleanup;
            call_args[captured + i + 1] = callback_atom(&scratch, encodings[i], scalar_value, &leaves[i]);
            if (!call_args[captured + i + 1]) goto cleanup;
        }
        Atom *value = NULL;
        if (lexical) {
            CettaCallOutcome returned;
            if (!cetta_prepared_pure_program_execute_controlled(
                    program, &scratch, call_args[1],
                    count + captured == 2u ? call_args[2] : call_args[1],
                    tensor_callback_stop, NULL, &returned) ||
                returned.kind != CETTA_CALL_VALUE ||
                returned.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
                !returned.term)
                goto cleanup;
            value = returned.term;
        } else {
            Atom *call = atom_expr(&scratch, call_args, count + captured + 1);
            CettaCallOutcome returned;
            if (!cetta_prepared_pure_program_rebind_closed_entry_call(program, call) ||
                !cetta_prepared_pure_program_execute_closed_controlled(program, &scratch,
                    65536u, tensor_callback_stop, NULL, &returned) ||
                returned.kind != CETTA_CALL_VALUE)
                goto cleanup;
            value = returned.term;
        }
        CettaTensorScalar converted;
        if (scalar(CETTA_TENSOR_F64, value, &converted) != CETTA_TENSOR_OK)
            goto cleanup;
        values[row] = converted.f64;
        cetta_prepared_pure_program_release_closed_execution(program);
        cetta_prepared_pure_program_clear_closed_entry_call(program);
        arena_reset(&scratch, mark);
    }
    if (!cetta_prepared_pure_program_is_current(program)) goto cleanup;
    /* The authored constructor returns COO. Preserve that explicit layout
     * observation too, including dense-only transport refusal. */
    CettaTensorStatus status = cetta_tensor_coo(CETTA_TENSOR_F64, cetta_tensor_shape(tensors[0]),
        cetta_tensor_rank(tensors[0]), keys, values, n, CETTA_TENSOR_REJECT, &result);
    if (status == CETTA_TENSOR_OK) answer = cetta_tensor_atom(destination, result);
cleanup:
    cetta_tensor_release(result); cetta_tensor_release(coordinates); cetta_tensor_release(unused);
    free(values); free(keys);
    cetta_prepared_pure_program_free(program);
    arena_free(&scratch); arena_free(&source);
    return answer;
}

static const char *const screen_index_kind = "tensor-screen-index-v1";
static const char *const screen_receipt_kind = "tensor-screen-receipt-v1";

static Atom *screen_owner(CettaLibraryContext *context, Arena *arena,
    const char *kind, void *payload, void (*release)(void *), size_t bytes) {
    uint64_t token;
    if (!cetta_native_handle_alloc_sized(context, kind, payload, release, bytes, &token)) {
        release(payload); return fault(arena, CETTA_TENSOR_NOMEM);
    }
    Atom *result = cetta_native_handle_owned_atom(context, arena, kind, token);
    if (!result) (void)cetta_native_handle_close(context, kind, token);
    return result ? result : fault(arena, CETTA_TENSOR_NOMEM);
}

static bool screen_call(CettaLibraryContext *context, Arena *arena,
    SymbolId op, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    bool prepare = op == g_builtin_syms.tensor_screen_index;
    bool apply = op == g_builtin_syms.tensor_screen;
    bool candidates = op == g_builtin_syms.tensor_screen_candidates;
    bool info = op == g_builtin_syms.tensor_screen_info;
    bool rerank = op == g_builtin_syms.tensor_rerank;
    bool source = op == g_builtin_syms.tensor_index_source;
    bool close = op == g_builtin_syms.tensor_close;
    if ((!prepare && !apply && !candidates && !info && !rerank && !source && !close) ||
        (apply ? nargs != 3u && nargs != 4u : nargs != 1u)) return false;
    uint64_t index_token = 0, receipt_token = 0;
    bool is_index = !prepare && cetta_native_handle_arg(args[0], screen_index_kind, &index_token);
    bool is_receipt = !prepare && cetta_native_handle_arg(args[0], screen_receipt_kind, &receipt_token);
    uint64_t token = is_index ? index_token : receipt_token;
    if (!prepare && !(apply ? is_index : source || close ? is_index || is_receipt : is_receipt))
        return false;
    if (eval_native_interrupted()) { *out = cetta_call_interrupted(); return true; }
    Atom *answer = NULL;
    if (prepare) {
        CettaTensor *matrix = cetta_tensor_from_atom(args[0]);
        if (!matrix) return false;
        CettaTensorScreenIndex *index = NULL;
        CettaTensorStatus status = cetta_tensor_screen_index(matrix, &index);
        if (eval_native_interrupted()) {
            cetta_tensor_screen_index_release(index); *out = cetta_call_interrupted(); return true;
        }
        answer = status == CETTA_TENSOR_OK
            ? screen_owner(context, arena, screen_index_kind, index, cetta_tensor_screen_index_release,
                cetta_tensor_screen_index_retained_bound(index))
            : fault(arena, status);
    } else if (apply) {
        CettaTensor *query = cetta_tensor_from_atom(args[1]);
        if (!query) return false;
        CettaTensor *mask = nargs == 4 ? cetta_tensor_from_atom(args[2]) : NULL;
        if (nargs == 4 && !mask && !atom_is_symbol(args[2], "all")) return false;
        CettaTensorScreenIndex *index = cetta_native_handle_get_owned(context, screen_index_kind, args[0]);
        size_t k;
        if (!index) answer = state_fault(arena, "closed-screen-index");
        else if (!natural(args[nargs - 1], &k)) answer = fault(arena, CETTA_TENSOR_VALUE);
        else {
            CettaTensorScreenReceipt *receipt = NULL;
            CettaTensorStatus status = cetta_tensor_screen(index, query, mask, k, &receipt);
            if (eval_native_interrupted()) {
                cetta_tensor_screen_receipt_release(receipt); *out = cetta_call_interrupted(); return true;
            }
            answer = status == CETTA_TENSOR_OK
                ? screen_owner(context, arena, screen_receipt_kind, receipt, cetta_tensor_screen_receipt_release,
                    cetta_tensor_screen_retained_bound(receipt))
                : fault(arena, status);
        }
    } else {
        const char *kind = is_index ? screen_index_kind : screen_receipt_kind;
        void *payload = cetta_native_handle_get_owned(context, kind, args[0]);
        if (!payload) answer = state_fault(arena, is_index ? "closed-screen-index" : "closed-screen-receipt");
        else if (close) {
            (void)cetta_native_handle_close(context, kind, token); answer = atom_unit(arena);
        } else if (source) {
            CettaTensor *value = is_index ? cetta_tensor_screen_index_source(payload)
                : cetta_tensor_screen_source(payload);
            answer = cetta_tensor_atom(arena, value);
        } else if (candidates) {
            answer = cetta_tensor_atom(arena, cetta_tensor_screen_candidates(payload));
        } else if (info) {
            CettaTensorScreenReceipt *receipt = payload;
            size_t k = cetta_tensor_screen_k(receipt);
            char text[32];
            (void)snprintf(text, sizeof(text), "%" PRIu64, (uint64_t)k);
            Atom *fields[] = {atom_symbol(arena, "TensorScreenInfo"),
                atom_symbol(arena, cetta_tensor_screen_is_certified(receipt) ? "CompleteSuperset" : "Unscreened"),
                atom_symbol(arena, cetta_tensor_screen_is_certified(receipt)
                    ? "NearestGradualSequential" : "FullScanFallback"),
                k <= INT64_MAX ? atom_int(arena, (int64_t)k) : atom_bigint(arena, text),
                cetta_tensor_atom(arena, cetta_tensor_screen_source(receipt)),
                cetta_tensor_atom(arena, cetta_tensor_screen_query(receipt)),
                cetta_tensor_screen_mask(receipt) ? cetta_tensor_atom(arena, cetta_tensor_screen_mask(receipt))
                    : atom_symbol(arena, "all"),
                cetta_tensor_atom(arena, cetta_tensor_screen_candidates(receipt)),
                cetta_tensor_screen_lower(receipt) ? cetta_tensor_atom(arena, cetta_tensor_screen_lower(receipt))
                    : atom_symbol(arena, "NoBounds"),
                cetta_tensor_screen_upper(receipt) ? cetta_tensor_atom(arena, cetta_tensor_screen_upper(receipt))
                    : atom_symbol(arena, "NoBounds")};
            answer = atom_expr(arena, fields, 10);
        } else {
            CettaTensor *indices = NULL, *scores = NULL;
            CettaTensorStatus status = cetta_tensor_screen_rerank(payload, &indices, &scores);
            if (eval_native_interrupted()) {
                cetta_tensor_release(indices); cetta_tensor_release(scores);
                *out = cetta_call_interrupted(); return true;
            }
            answer = status == CETTA_TENSOR_OK ? atom_expr(arena, (Atom *[]){
                atom_symbol(arena, "TensorRanking"), atom_symbol(arena, "Exact"),
                cetta_tensor_atom(arena, indices), cetta_tensor_atom(arena, scores)}, 4) : fault(arena, status);
            cetta_tensor_release(indices); cetta_tensor_release(scores);
        }
    }
    *out = cetta_call_completed_value(answer);
    return true;
}

bool cetta_tensor_call(CettaLibraryContext *context,Space *space,Arena *a,Atom *head,Atom **args,uint32_t nargs,CettaCallOutcome *out) {
    if(!head || head->kind!=ATOM_SYMBOL) return false;
    SymbolId op=atom_symbol_current_id(head);
    if (op == g_builtin_syms.tensor_is_tensor) {
        if (nargs != 1u) return false;
        Atom *answer = scalar_atom(a, CETTA_TENSOR_BOOL,
            (CettaTensorScalar){.u8 = cetta_tensor_from_atom(args[0]) != NULL});
        *out = cetta_call_completed_value(answer);
        return true;
    }
    if (screen_call(context, a, op, args, nargs, out)) return true;
    if (op == g_builtin_syms.tensor_encode_bytes || op == g_builtin_syms.tensor_decode_bytes) {
        if (nargs != 1u) return false;
        CettaTensor *input = cetta_tensor_from_atom(args[0]), *result = NULL;
        if (!input) return false;
        unsigned char *bytes = NULL;
        size_t size = 0;
        CettaTensorStatus status;
        if (op == g_builtin_syms.tensor_encode_bytes) {
            status = cetta_tensor_encode(input, &bytes, &size);
            if (status == CETTA_TENSOR_OK) {
                status = cetta_tensor_new_take(CETTA_TENSOR_U8, &size, 1, bytes, size, &result);
                if (status == CETTA_TENSOR_OK) bytes = NULL;
            }
        } else if (cetta_tensor_dtype(input) != CETTA_TENSOR_U8) status = CETTA_TENSOR_DTYPE;
        else if (cetta_tensor_rank(input) != 1 || cetta_tensor_stored(input) != cetta_tensor_count(input))
            status = CETTA_TENSOR_ENCODING;
        else {
            size = cetta_tensor_count(input);
            bytes = size ? malloc(size) : NULL;
            status = size && !bytes ? CETTA_TENSOR_NOMEM : cetta_tensor_copy_values(input, bytes, size);
            if (status == CETTA_TENSOR_ENCODING) {
                status = CETTA_TENSOR_OK;
                for (size_t i = 0; i < size; i++) {
                    if (eval_native_interrupted()) {
                        free(bytes);
                        *out = cetta_call_interrupted();
                        return true;
                    }
                    bool present = false;
                    CettaTensorScalar value;
                    status = cetta_tensor_get(input, i, &present, &value);
                    if (status != CETTA_TENSOR_OK || !present) { status = CETTA_TENSOR_ENCODING; break; }
                    bytes[i] = value.u8;
                }
            }
            if (status == CETTA_TENSOR_OK) status = cetta_tensor_decode(bytes, size, &result);
        }
        free(bytes);
        *out = cetta_call_completed_value(publish(a, status, result));
        return true;
    }
    if (op == g_builtin_syms.tensor_scan_sum || op == g_builtin_syms.tensor_concatenate ||
        op == g_builtin_syms.tensor_permute) {
        bool concat = op == g_builtin_syms.tensor_concatenate;
        if (nargs != (concat ? 3u : 2u)) return false;
        CettaTensor *first = cetta_tensor_from_atom(args[0]);
        CettaTensor *second = concat ? cetta_tensor_from_atom(args[1]) : NULL;
        if (!first || (concat && !second)) return false;
        CettaTensor *result = NULL;
        CettaTensorStatus status;
        if (op == g_builtin_syms.tensor_permute) {
            size_t *axes = NULL, rank = 0;
            status = shape(a, args[1], &axes, &rank);
            if (status == CETTA_TENSOR_OK) status = cetta_tensor_permute(first, axes, rank, &result);
            free(axes);
        } else {
            size_t axis = 0;
            if (!natural(args[concat ? 2u : 1u], &axis)) status = CETTA_TENSOR_SHAPE;
            else status = concat ? cetta_tensor_concatenate(first, second, axis, &result)
                : cetta_tensor_scan_sum(first, axis, &result);
        }
        *out = cetta_call_completed_value(publish(a, status, result));
        return true;
    }
    if (op == g_builtin_syms.tensor_entries || op == g_builtin_syms.tensor_gather || op == g_builtin_syms.tensor_gather_rows ||
        op == g_builtin_syms.tensor_merge || op == g_builtin_syms.lib_tensor_try_zip_map) {
        bool map = op == g_builtin_syms.lib_tensor_try_zip_map;
        if (nargs != (map ? 4u : op == g_builtin_syms.tensor_merge ? 3u :
                      (op == g_builtin_syms.tensor_gather || op == g_builtin_syms.tensor_gather_rows) ? 2u : 1u)) return false;
        CettaTensor *first = map ? NULL : cetta_tensor_from_atom(args[0]);
        if (map) {
            Atom *const *inputs; CettaExprLen n;
            if (!sequence(a, args[1], &inputs, &n) || !n) return false;
            for (size_t i = 0; i < n; i++) if (!cetta_tensor_from_atom(inputs[i])) return false;
            Atom *value = zip_map(space, a, args);
            if (eval_native_interrupted()) *out = cetta_call_interrupted();
            else if (value) *out = cetta_call_completed_value(
                atom_expr2(a, atom_symbol(a, "Tensor:Mapped"), value));
            else {
                /* The authored route runs in the call's ambient Space.  A
                 * raw GV_SPACE in this result would become a borrowed
                 * resource in Prime's persistent Need path and needlessly
                 * prevent continuation collection. */
                *out = cetta_call_completed_value(
                    atom_symbol(a, "Tensor:Fallback"));
            }
            return true;
        }
        if (!first) return false;
        CettaTensor *second = nargs > 1 ? cetta_tensor_from_atom(args[1]) : NULL;
        if (nargs > 1 && !second) return false;
        CettaTensor *result = NULL, *values = NULL;
        CettaTensorStatus status;
        if (op == g_builtin_syms.tensor_entries) {
            status = cetta_tensor_entries(first, &result, &values);
            *out = cetta_call_completed_value(status == CETTA_TENSOR_OK
                ? atom_expr3(a, atom_symbol(a, "TensorEntries"), cetta_tensor_atom(a, result), cetta_tensor_atom(a, values))
                : fault(a, status));
        } else {
            if (op == g_builtin_syms.tensor_gather)
                status = cetta_tensor_gather(first, second, &result);
            else if (op == g_builtin_syms.tensor_gather_rows)
                status = cetta_tensor_gather_rows(first, second, &result);
            else {
                CettaTensorDuplicates policy = CETTA_TENSOR_REJECT;
                if (atom_is_symbol(args[2], "first")) policy = CETTA_TENSOR_FIRST;
                else if (atom_is_symbol(args[2], "last")) policy = CETTA_TENSOR_LAST;
                else if (atom_is_symbol(args[2], "sum")) policy = CETTA_TENSOR_SUM;
                else if (!atom_is_symbol(args[2], "reject")) {
                    *out = cetta_call_completed_value(fault(a, CETTA_TENSOR_VALUE)); return true;
                }
                status = cetta_tensor_merge(first, second, policy, &result);
            }
            *out = cetta_call_completed_value(status == CETTA_TENSOR_OK
                ? cetta_tensor_atom(a, result) : fault(a, status));
        }
        cetta_tensor_release(result); cetta_tensor_release(values); return true;
    }
    if(op==g_builtin_syms.tensor_from_coordinates) {
        if(nargs!=3 && nargs!=4) return false;
        Atom *policy=nargs==4 ? args[3] : atom_symbol(a,"reject");
        if(policy && policy->kind==ATOM_EXPR && policy->expr.len==2 && atom_is_symbol(policy->expr.elems[0],"combine")) return false;
        CettaTensor *indices=cetta_tensor_from_atom(args[1]),*values=cetta_tensor_from_atom(args[2]);
        if(!indices || !values) return false;
        Atom *answer=from_coordinates(a,args[0],indices,values,policy);
        *out=answer ? cetta_call_completed_value(answer) : cetta_call_interrupted();
        return true;
    }
    bool cosine_prepare=op==g_builtin_syms.tensor_cosine_index;
    bool cosine_scores=op==g_builtin_syms.tensor_cosine_scores, index_source=op==g_builtin_syms.tensor_index_source;
    bool retrieve=op==g_builtin_syms.tensor_cosine_retrieve;
    uint64_t cosine_token;
    bool cosine_close=op==g_builtin_syms.tensor_close && nargs==1 && cetta_native_handle_arg(args[0],cosine_kind,&cosine_token);
    if(cosine_prepare || cosine_scores || retrieve || index_source || cosine_close) {
        if(cosine_prepare ? nargs!=1 && nargs!=2 : retrieve ? nargs!=3 && nargs!=4 :
            nargs!=(cosine_scores ? 2u : 1u)) return false;
        bool screened=cosine_prepare && nargs==2 && atom_is_symbol(args[1],"screened");
        if(cosine_prepare && nargs==2 && !screened) return false;
        if(eval_native_interrupted()) { *out=cetta_call_interrupted(); return true; }
        Atom *answer=NULL; CettaTensorStatus status=CETTA_TENSOR_OK;
        if(cosine_prepare) {
            CettaTensor *source=cetta_tensor_from_atom(args[0]); if(!source) return false;
            if(cetta_tensor_rank(source)!=2) answer=fault(a,CETTA_TENSOR_SHAPE);
            else {
                CettaTensor *normalized=NULL;
                status=cetta_tensor_normalize_rows(source,&normalized);
                if(status!=CETTA_TENSOR_OK) answer=fault(a,status);
                else {
                    TensorCosineIndex *index=malloc(sizeof(*index));
                    if(!index) { cetta_tensor_release(normalized); answer=fault(a,CETTA_TENSOR_NOMEM); }
                    else {
                        cetta_tensor_retain(source); *index=(TensorCosineIndex){source,normalized,NULL};
                        if(screened) status=cetta_tensor_screen_index(normalized,&index->screen);
                        if(eval_native_interrupted()) {
                            cosine_release(index); *out=cetta_call_interrupted(); return true;
                        }
                        uint64_t token;
                        if(status!=CETTA_TENSOR_OK) { cosine_release(index); answer=fault(a,status); }
                        else if(!cetta_native_handle_alloc_sized(context,cosine_kind,index,cosine_release,
                            cosine_retained_bound(index),&token)) {
                            cosine_release(index); answer=fault(a,CETTA_TENSOR_NOMEM);
                        } else {
                            answer=cetta_native_handle_owned_atom(context,a,cosine_kind,token);
                            if(!answer) { (void)cetta_native_handle_close(context,cosine_kind,token); answer=fault(a,CETTA_TENSOR_NOMEM); }
                        }
                    }
                }
            }
        } else {
            uint64_t token;
            if(!cetta_native_handle_arg(args[0],cosine_kind,&token)) return false;
            TensorCosineIndex *index=cetta_native_handle_get_owned(context, cosine_kind, args[0]);
            if(!index) answer=state_fault(a,"closed-or-forged-index");
            else if(cosine_close) { (void)cetta_native_handle_close(context,cosine_kind,token); answer=atom_unit(a); }
            else if(index_source) answer=cetta_tensor_atom(a,index->source);
            else {
                CettaTensor *query=cetta_tensor_from_atom(args[1]); if(!query) return false;
                CettaTensor *normalized=NULL,*scores=NULL,*indices=NULL;
                CettaTensor *mask=retrieve && nargs==4 ? cetta_tensor_from_atom(args[2]) : NULL;
                if(retrieve && nargs==4 && !mask && !atom_is_symbol(args[2],"all")) return false;
                size_t k=0;
                if(cetta_tensor_rank(query)!=1) status=CETTA_TENSOR_SHAPE;
                else if(retrieve && !natural(args[nargs-1],&k)) status=CETTA_TENSOR_VALUE;
                else status=cetta_tensor_normalize_rows(query,&normalized);
                if(status==CETTA_TENSOR_OK) {
                    if(retrieve) status=cosine_retrieve(index,normalized,mask,k,&indices,&scores);
                    else status=cetta_tensor_matvec(index->normalized,normalized,CETTA_TENSOR_SEQUENTIAL,&scores);
                }
                cetta_tensor_release(normalized);
                if(eval_native_interrupted()) {
                    cetta_tensor_release(indices); cetta_tensor_release(scores);
                    *out=cetta_call_interrupted(); return true;
                }
                if(!retrieve) answer=publish(a,status,scores);
                else {
                    answer=status==CETTA_TENSOR_OK ? atom_expr(a,(Atom *[]){atom_symbol(a,"TensorRanking"),
                        atom_symbol(a,"Exact"),cetta_tensor_atom(a,indices),cetta_tensor_atom(a,scores)},4) : fault(a,status);
                    cetta_tensor_release(indices); cetta_tensor_release(scores);
                }
            }
        }
        *out=cetta_call_completed_value(answer); return true;
    }
#if CETTA_BUILD_WITH_JSON_GSLT
    if(op==g_builtin_syms.tensor_from_json) {
        if(nargs!=4 || !dtype(args[0])) return false;
        if(eval_native_interrupted()) { *out=cetta_call_interrupted(); return true; }
        Atom *answer=from_json(a,args);
        *out=eval_native_interrupted() ? cetta_call_interrupted() : cetta_call_completed_value(answer);
        return true;
    }
#endif
    bool make=op==g_builtin_syms.tensor_from_values,coo=op==g_builtin_syms.tensor_from_coo;
    bool builder=op==g_builtin_syms.tensor_builder,put=op==g_builtin_syms.tensor_put,
        freeze=op==g_builtin_syms.tensor_freeze,freeze_log=op==g_builtin_syms.tensor_freeze_log,
        close=op==g_builtin_syms.tensor_close;
    bool unary=op==g_builtin_syms.tensor_inspect || op==g_builtin_syms.tensor_shape || op==g_builtin_syms.tensor_dtype ||
        op==g_builtin_syms.tensor_layout || op==g_builtin_syms.tensor_copy || op==g_builtin_syms.tensor_encode ||
        op==g_builtin_syms.tensor_all_finite || op==g_builtin_syms.tensor_normalize;
    bool get=op==g_builtin_syms.tensor_at,convert=op==g_builtin_syms.tensor_convert,view=op==g_builtin_syms.tensor_view;
    bool reshape=op==g_builtin_syms.tensor_reshape;
    bool cast=op==g_builtin_syms.tensor_cast,promote=op==g_builtin_syms.tensor_promote;
    bool sum=op==g_builtin_syms.tensor_sum,matvec=op==g_builtin_syms.tensor_matvec,topk=op==g_builtin_syms.tensor_top_k;
    bool decode=op==g_builtin_syms.tensor_decode;
    if(!make && !coo && !builder && !put && !freeze && !freeze_log && !close && !unary && !get && !convert && !cast && !promote && !view && !reshape && !sum && !matvec && !topk && !decode) return false;
    if(eval_native_interrupted()) { *out=cetta_call_interrupted(); return true; }
    Atom *normalized[4];
    if((coo && nargs==3) || (builder && nargs==2) || (sum && nargs==1) || (matvec && nargs==2) || (topk && nargs==2) || (cast && nargs==2)) {
        for(uint32_t i=0;i<nargs;i++) normalized[i]=args[i];
        if(topk) {
            normalized[2]=args[1]; normalized[1]=atom_symbol(a,"all");
        } else normalized[nargs]=atom_symbol(a,cast ? "exact" : coo || builder ? "reject" : "sequential");
        args=normalized; nargs++;
    }
    uint32_t arity=make || builder || put || cast ? 3 : coo || view ? 4 : unary || freeze || freeze_log || close || decode ? 1 : matvec || topk ? 3 : 2;
    if(nargs!=arity) return false;
    if((make || coo || builder) && !dtype(args[0])) return false;
    if(coo && args[3] && args[3]->kind==ATOM_EXPR && args[3]->expr.len==2 &&
        atom_is_symbol(args[3]->expr.elems[0],"combine")) return false;
    if(builder && args[2] && args[2]->kind==ATOM_EXPR && args[2]->expr.len==2 &&
        atom_is_symbol(args[2]->expr.elems[0],"combine")) return false;
    CettaTensor *t=(!make && !coo && !builder && !put && !freeze && !freeze_log && !close && !decode) ? cetta_tensor_from_atom(args[0]) : NULL;
    if(!make && !coo && !builder && !put && !freeze && !freeze_log && !close && !decode && !t) return false;
    Atom *answer=NULL; CettaTensor *result=NULL; CettaTensorStatus s=CETTA_TENSOR_OK;
    if(make || coo) answer=construct(a,args,coo);
    else if(builder) answer=builder_new(context,a,args);
    else if(put || freeze || freeze_log || close) {
        uint64_t token;
        if(!cetta_native_handle_arg(args[0],builder_kind,&token)) return false;
        TensorBuilder *b=cetta_native_handle_get_owned(context, builder_kind, args[0]);
        if(!b) answer=state_fault(a,"closed-or-forged-builder");
        else if(put) answer=builder_put(a,b,args);
        else if(freeze_log) {
            if(b->frozen) answer=state_fault(a,"frozen-builder");
            else if(b->length>SIZE_MAX/sizeof(Atom *) || b->rank>SIZE_MAX/sizeof(Atom *)) answer=fault(a,CETTA_TENSOR_OVERFLOW);
            else {
                Atom **entries=b->length ? arena_alloc(a,b->length*sizeof(*entries)) : NULL;
                Atom **dimensions=b->rank ? arena_alloc(a,b->rank*sizeof(*dimensions)) : NULL;
                size_t width=cetta_tensor_itemsize(b->dtype);
                for(size_t i=0;i<b->length;i++) {
                    CettaTensorScalar value={0}; memcpy(&value,b->values+i*width,width);
                    entries[i]=atom_expr2(a,scalar_atom(a,CETTA_TENSOR_INDEX,(CettaTensorScalar){.index=b->coordinates[i]}),scalar_atom(a,b->dtype,value));
                }
                for(size_t i=0;i<b->rank;i++) dimensions[i]=scalar_atom(a,CETTA_TENSOR_INDEX,(CettaTensorScalar){.index=b->shape[i]});
                answer=atom_expr(a,(Atom *[]){atom_symbol(a,"TensorLog"),atom_symbol(a,cetta_tensor_dtype_name(b->dtype)),
                    atom_expr(a,dimensions,b->rank),atom_expr(a,entries,b->length)},4);
                b->frozen=true; free(b->coordinates); free(b->values); b->coordinates=NULL; b->values=NULL; b->capacity=0;
            }
        } else if(freeze) {
            if(b->frozen) answer=state_fault(a,"frozen-builder");
            else {
                s=cetta_tensor_coo(b->dtype,b->shape,b->rank,b->coordinates,b->values,b->length,b->duplicates,&result);
                answer=publish(a,s,result);
                if(s==CETTA_TENSOR_OK && cetta_tensor_from_atom(answer)) {
                    b->frozen=true; free(b->coordinates); free(b->values); b->coordinates=NULL; b->values=NULL; b->capacity=0;
                }
            }
        } else {
            uint64_t id; if(cetta_native_handle_arg(args[0],builder_kind,&id)) (void)cetta_native_handle_close(context,builder_kind,id);
            answer=atom_unit(a);
        }
    } else if(op==g_builtin_syms.tensor_all_finite) answer=scalar_atom(a,CETTA_TENSOR_BOOL,(CettaTensorScalar){.u8=cetta_tensor_all_finite(t)});
    else if(op==g_builtin_syms.tensor_normalize) { s=cetta_tensor_normalize_rows(t,&result); answer=publish(a,s,result); }
    else if(op==g_builtin_syms.tensor_inspect) {
        answer=atom_structural_inspect(args[0],a);
        if(answer && cetta_tensor_dtype(t)==CETTA_TENSOR_BOOL && eval_current_language_id()==CETTA_LANGUAGE_PETTA) {
            Atom *entries=answer->expr.elems[3];
            size_t n=(size_t)entries->expr.len; Atom **rows=n ? arena_alloc(a,n*sizeof(*rows)) : NULL;
            for(size_t i=0;i<n;i++) {
                Atom *row=entries->expr.elems[i];
                rows[i]=atom_expr2(a,row->expr.elems[0],atom_symbol(a,row->expr.elems[1]->ground.bval ? "true" : "false"));
            }
            answer=atom_expr(a,(Atom *[]){answer->expr.elems[0],answer->expr.elems[1],answer->expr.elems[2],atom_expr(a,rows,n)},4);
        }
    }
    else if(op==g_builtin_syms.tensor_dtype) answer=atom_symbol(a,cetta_tensor_dtype_name(cetta_tensor_dtype(t)));
    else if(op==g_builtin_syms.tensor_layout) answer=atom_symbol(a,cetta_tensor_layout_name(cetta_tensor_layout(t)));
    else if(op==g_builtin_syms.tensor_shape) {
        size_t rank=cetta_tensor_rank(t); const size_t *dimensions=cetta_tensor_shape(t);
        Atom **items=rank ? arena_alloc(a,rank*sizeof(*items)) : NULL;
        for(size_t i=0;i<rank;i++) items[i]=scalar_atom(a,CETTA_TENSOR_INDEX,(CettaTensorScalar){.index=dimensions[i]});
        answer=atom_expr(a,items,rank);
    } else if(cast || promote) {
        CettaTensorDType destination=dtype(args[1]);
        CettaTensorCastMode mode=CETTA_TENSOR_CAST_EXACT;
        if(cast) {
            if(atom_is_symbol(args[2],"nearest")) mode=CETTA_TENSOR_CAST_NEAREST;
            else if(atom_is_symbol(args[2],"truncate")) mode=CETTA_TENSOR_CAST_TRUNCATE;
            else if(!atom_is_symbol(args[2],"exact")) s=CETTA_TENSOR_VALUE;
        }
        if(!destination) s=CETTA_TENSOR_DTYPE;
        if(s==CETTA_TENSOR_OK && promote && !cetta_tensor_exact_promotion(cetta_tensor_dtype(t),destination)) s=CETTA_TENSOR_PROMOTION;
        if(s==CETTA_TENSOR_OK) s=cetta_tensor_cast(t,destination,mode,&result);
        answer=publish(a,s,result);
    } else if(get) {
        size_t flat; bool present; CettaTensorScalar x;
        if(!coordinate(a,args[1],cetta_tensor_shape(t),cetta_tensor_rank(t),cetta_tensor_count(t),&flat)) answer=fault(a,CETTA_TENSOR_INDEX_ERROR);
        else {
            s=cetta_tensor_get(t,flat,&present,&x);
            answer=s!=CETTA_TENSOR_OK ? fault(a,s) : !present ? atom_symbol(a,"tensor:absent") :
                atom_expr2(a,atom_symbol(a,"tensor:present"),scalar_atom(a,cetta_tensor_dtype(t),x));
        }
    } else if(convert) {
        bool recognized=false;
#define LAYOUT(name, code, text, support, order) if(atom_is_symbol(args[1],text)) { recognized=true; s=cetta_tensor_convert(t,CETTA_TENSOR_##name,&result); }
        CETTA_TENSOR_LAYOUT_ROWS(LAYOUT)
#undef LAYOUT
        answer=recognized ? publish(a,s,result) : fault(a,CETTA_TENSOR_VALUE);
    } else if(reshape) {
        size_t *dimensions=NULL,rank=0;
        s=shape(a,args[1],&dimensions,&rank);
        if(s==CETTA_TENSOR_OK) s=cetta_tensor_reshape(t,dimensions,rank,&result);
        free(dimensions); answer=publish(a,s,result);
    } else if(view) {
        size_t *dimensions=NULL,rank=0,offset=0; Atom *const *items=NULL; CettaExprLen n=0;
        s=shape(a,args[1],&dimensions,&rank);
        ptrdiff_t *strides=s==CETTA_TENSOR_OK && rank ? malloc(rank*sizeof(*strides)) : NULL;
        if(s==CETTA_TENSOR_OK && rank && !strides) s=CETTA_TENSOR_NOMEM;
        if(s==CETTA_TENSOR_OK && (!sequence(a,args[2],&items,&n) || n!=rank || !natural(args[3],&offset))) s=CETTA_TENSOR_SHAPE;
        for(size_t i=0;s==CETTA_TENSOR_OK && i<rank;i++) {
            if(!items[i] || items[i]->kind!=ATOM_GROUNDED || items[i]->ground.gkind!=GV_INT ||
                items[i]->ground.ival<PTRDIFF_MIN || items[i]->ground.ival>PTRDIFF_MAX) s=CETTA_TENSOR_VALUE;
            else strides[i]=(ptrdiff_t)items[i]->ground.ival;
        }
        if(s==CETTA_TENSOR_OK) s=cetta_tensor_view(t,dimensions,strides,rank,offset,&result);
        free(dimensions); free(strides); answer=publish(a,s,result);
    } else if(sum || matvec) {
        Atom *policy=args[sum ? 1 : 2]; CettaTensorSchedule schedule=CETTA_TENSOR_SEQUENTIAL;
        if(atom_is_symbol(policy,"fixed4")) schedule=CETTA_TENSOR_FIXED4;
        else if(!atom_is_symbol(policy,"sequential")) s=CETTA_TENSOR_VALUE;
        if(sum) {
            CettaTensorScalar x;
            if(s==CETTA_TENSOR_OK) s=cetta_tensor_sum(t,schedule,&x);
            CettaTensorDType d=cetta_tensor_dtype(t); if(d==CETTA_TENSOR_F32) d=CETTA_TENSOR_F64;
            answer=s==CETTA_TENSOR_OK ? scalar_atom(a,d,x) : fault(a,s);
        } else {
            CettaTensor *vector=cetta_tensor_from_atom(args[1]); if(!vector) return false;
            if(s==CETTA_TENSOR_OK) s=cetta_tensor_matvec(t,vector,schedule,&result);
            answer=publish(a,s,result);
        }
    } else if(topk) {
        CettaTensor *mask=atom_is_symbol(args[1],"all") ? NULL : cetta_tensor_from_atom(args[1]); size_t k;
        if((!mask && !atom_is_symbol(args[1],"all")) || !natural(args[2],&k)) s=CETTA_TENSOR_VALUE;
        else s=cetta_tensor_top_k(t,mask,k,&result);
        answer=publish(a,s,result);
    } else if(op==g_builtin_syms.tensor_copy) {
        s=cetta_tensor_copy(t,&result); answer=publish(a,s,result);
    }
    else if(op==g_builtin_syms.tensor_encode) {
        unsigned char *bytes=NULL; size_t length=0;
        s=cetta_tensor_encode(t,&bytes,&length);
        answer=s==CETTA_TENSOR_OK ? atom_string_n(a,(const char *)bytes,length) : fault(a,s); free(bytes);
    } else if(decode) {
        if(!args[0] || args[0]->kind!=ATOM_GROUNDED || args[0]->ground.gkind!=GV_STRING) return false;
        s=cetta_tensor_decode((const unsigned char *)args[0]->ground.sval,atom_string_len(args[0]),&result); answer=publish(a,s,result);
    }
    *out=cetta_call_completed_value(answer ? answer : fault(a,CETTA_TENSOR_NOMEM)); return true;
}
