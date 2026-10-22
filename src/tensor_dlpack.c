#include "tensor_dlpack.h"
#include <limits.h>
#include <stdlib.h>

static CettaTensorDType dtype(DLDataType type) {
    if(type.lanes!=1) return 0;
    if(type.code==kDLInt && type.bits==64) return CETTA_TENSOR_I64;
    if(type.code==kDLUInt && type.bits==64) return CETTA_TENSOR_INDEX;
    if(type.code==kDLUInt && type.bits==8) return CETTA_TENSOR_U8;
    if(type.code==kDLFloat && type.bits==64) return CETTA_TENSOR_F64;
    if(type.code==kDLFloat && type.bits==32) return CETTA_TENSOR_F32;
    if(type.code==kDLBool && type.bits==8) return CETTA_TENSOR_BOOL;
    return 0;
}
static bool address_add(uintptr_t base,ptrdiff_t offset,uintptr_t *out) {
    if(offset>=0) return !__builtin_add_overflow(base,(uintptr_t)offset,out);
    uintptr_t magnitude=(uintptr_t)(-(offset+1))+1;
    return !__builtin_sub_overflow(base,magnitude,out);
}
CettaTensorStatus cetta_tensor_dlpack_copy(const DLTensor *source,
    const void *allocation_base,size_t allocation_bytes,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!source || source->device.device_type!=kDLCPU || source->device.device_id!=0 ||
        source->ndim<0 || (source->ndim && !source->shape)) return CETTA_TENSOR_VALUE;
    CettaTensorDType type=dtype(source->dtype);
    if(!type) return CETTA_TENSOR_DTYPE;
    size_t rank=(size_t)source->ndim,width=cetta_tensor_itemsize(type);
    if(rank>SIZE_MAX/sizeof(size_t) || rank>SIZE_MAX/sizeof(ptrdiff_t)) return CETTA_TENSOR_OVERFLOW;
    size_t *shape=rank ? malloc(rank*sizeof(*shape)) : NULL;
    ptrdiff_t *strides=rank ? malloc(rank*sizeof(*strides)) : NULL;
    if(rank && (!shape || !strides)) { free(shape); free(strides); return CETTA_TENSOR_NOMEM; }
    CettaTensorStatus status=CETTA_TENSOR_OK;
    for(size_t i=0;i<rank;i++) {
        if(source->shape[i]<0 || (uint64_t)source->shape[i]>SIZE_MAX) { status=CETTA_TENSOR_SHAPE; break; }
        shape[i]=(size_t)source->shape[i];
    }
    size_t count=0,bytes=0;
    if(status==CETTA_TENSOR_OK) status=cetta_tensor_byte_count(type,shape,rank,&count,&bytes);
    ptrdiff_t low=0,high=0; uintptr_t origin=0,minimum=0,maximum=0,span_end=0;
    if(status==CETTA_TENSOR_OK && count) {
        if(!source->data || !allocation_base || source->byte_offset>SIZE_MAX ||
            __builtin_add_overflow((uintptr_t)source->data,(uintptr_t)source->byte_offset,&origin) ||
            __builtin_add_overflow((uintptr_t)allocation_base,(uintptr_t)allocation_bytes,&span_end)) status=CETTA_TENSOR_OVERFLOW;
        size_t contiguous=width;
        for(size_t i=rank;status==CETTA_TENSOR_OK && i>0;i--) {
            size_t axis=i-1; ptrdiff_t stride;
            if(source->strides) {
                int64_t supplied=source->strides[axis];
                if(supplied<PTRDIFF_MIN || supplied>PTRDIFF_MAX ||
                    __builtin_mul_overflow((ptrdiff_t)supplied,(ptrdiff_t)width,&stride)) { status=CETTA_TENSOR_OVERFLOW; break; }
            } else {
                if(contiguous>PTRDIFF_MAX) { status=CETTA_TENSOR_OVERFLOW; break; }
                stride=(ptrdiff_t)contiguous;
            }
            strides[axis]=stride;
            ptrdiff_t extent;
            if(shape[axis]-1>PTRDIFF_MAX || __builtin_mul_overflow((ptrdiff_t)(shape[axis]-1),stride,&extent) ||
                (extent<0 ? __builtin_add_overflow(low,extent,&low) : __builtin_add_overflow(high,extent,&high))) {
                status=CETTA_TENSOR_OVERFLOW; break;
            }
            if(!source->strides && __builtin_mul_overflow(contiguous,shape[axis],&contiguous)) status=CETTA_TENSOR_OVERFLOW;
        }
        if(status==CETTA_TENSOR_OK &&
            (!address_add(origin,low,&minimum) || !address_add(origin,high,&maximum) ||
             minimum<(uintptr_t)allocation_base || maximum>span_end || width>span_end-maximum)) status=CETTA_TENSOR_INDEX_ERROR;
    }
    unsigned char *data=status==CETTA_TENSOR_OK && bytes ? malloc(bytes) : NULL;
    if(status==CETTA_TENSOR_OK && bytes && !data) status=CETTA_TENSOR_NOMEM;
    for(size_t flat=0;status==CETTA_TENSOR_OK && flat<count;flat++) {
        size_t rest=flat; ptrdiff_t offset=0;
        for(size_t i=rank;i>0;i--) {
            size_t axis=i-1,coordinate=rest%shape[axis]; rest/=shape[axis];
            /* Admission bounded the sum of all positive and all negative
             * axis extents, so every partial coordinate sum fits too. */
            offset+=(ptrdiff_t)coordinate*strides[axis];
        }
        uintptr_t location; if(!address_add(origin,offset,&location)) { status=CETTA_TENSOR_OVERFLOW; break; }
        memcpy(data+flat*width,(const void *)location,width);
    }
    if(status==CETTA_TENSOR_OK) {
        status=cetta_tensor_new_take(type,shape,rank,data,bytes,out);
        if(status==CETTA_TENSOR_OK) data=NULL;
    }
    free(data); free(shape); free(strides); return status;
}
CettaTensorStatus cetta_tensor_dlpack_consume(DLManagedTensor *source,
    const void *base,size_t bytes,CettaTensor **out) {
    CettaTensorStatus status=cetta_tensor_dlpack_copy(source ? &source->dl_tensor : NULL,base,bytes,out);
    if(source && source->deleter) source->deleter(source);
    return status;
}
CettaTensorStatus cetta_tensor_dlpack_consume_versioned(DLManagedTensorVersioned *source,
    const void *base,size_t bytes,CettaTensor **out) {
    CettaTensorStatus status=CETTA_TENSOR_VALUE;
    if(out) *out=NULL;
    /* A different major permits reading only the stable deleter prefix. */
    if(source && source->version.major==1 &&
        !(source->flags&~(DLPACK_FLAG_BITMASK_READ_ONLY|DLPACK_FLAG_BITMASK_IS_COPIED)) &&
        (source->version.minor<2 || !source->dl_tensor.ndim || source->dl_tensor.strides))
        status=cetta_tensor_dlpack_copy(&source->dl_tensor,base,bytes,out);
    if(source && source->deleter) source->deleter(source);
    return status;
}
typedef struct {
    DLManagedTensorVersioned versioned;
    DLManagedTensor legacy;
    void *data;
    int64_t *shape,*strides;
} ExportOwner;
static void release_owner(ExportOwner *owner) {
    free(owner->data); free(owner->shape); free(owner->strides); free(owner);
}
static void release_versioned(DLManagedTensorVersioned *tensor) { release_owner(tensor->manager_ctx); }
static void release_legacy(DLManagedTensor *tensor) { release_owner(tensor->manager_ctx); }
static CettaTensorStatus export_owner(const CettaTensor *source,ExportOwner **out) {
    *out=NULL;
    if(!source || cetta_tensor_layout(source)!=CETTA_TENSOR_DENSE) return CETTA_TENSOR_ENCODING;
    size_t rank=cetta_tensor_rank(source),bytes,width=cetta_tensor_itemsize(cetta_tensor_dtype(source)),count;
    if(rank>INT32_MAX || rank>SIZE_MAX/sizeof(int64_t)) return CETTA_TENSOR_OVERFLOW;
    CettaTensorStatus status=cetta_tensor_byte_count(cetta_tensor_dtype(source),cetta_tensor_shape(source),rank,&count,&bytes);
    if(status!=CETTA_TENSOR_OK) return status;
    ExportOwner *owner=calloc(1,sizeof(*owner)); if(!owner) return CETTA_TENSOR_NOMEM;
    owner->shape=rank ? malloc(rank*sizeof(int64_t)) : NULL;
    owner->strides=rank ? malloc(rank*sizeof(int64_t)) : NULL;
    owner->data=bytes ? malloc(bytes) : NULL;
    if((rank && (!owner->shape || !owner->strides)) || (bytes && !owner->data)) { release_owner(owner); return CETTA_TENSOR_NOMEM; }
    size_t step=1;
    for(size_t i=rank;i>0;i--) {
        size_t axis=i-1,dimension=cetta_tensor_shape(source)[axis];
        if(dimension>INT64_MAX || step>INT64_MAX) { release_owner(owner); return CETTA_TENSOR_OVERFLOW; }
        owner->shape[axis]=(int64_t)dimension; owner->strides[axis]=(int64_t)step;
        if(__builtin_mul_overflow(step,dimension,&step)) { release_owner(owner); return CETTA_TENSOR_OVERFLOW; }
    }
    status=cetta_tensor_copy_values(source,owner->data,bytes);
    if(status!=CETTA_TENSOR_OK) { release_owner(owner); return status; }
    CettaTensorDType scalar=cetta_tensor_dtype(source);
    DLDataType datatype={.code=scalar==CETTA_TENSOR_I64 ? kDLInt : scalar==CETTA_TENSOR_INDEX || scalar==CETTA_TENSOR_U8 ? kDLUInt : scalar==CETTA_TENSOR_BOOL ? kDLBool : kDLFloat,
                         .bits=(uint8_t)(width*8),.lanes=1};
    DLTensor value={.data=owner->data,.device={kDLCPU,0},.ndim=(int32_t)rank,.dtype=datatype,
                    .shape=owner->shape,.strides=owner->strides,.byte_offset=0};
    owner->versioned=(DLManagedTensorVersioned){.version={1,0},.manager_ctx=owner,.deleter=release_versioned,
        .flags=DLPACK_FLAG_BITMASK_IS_COPIED,.dl_tensor=value};
    owner->legacy=(DLManagedTensor){.dl_tensor=value,.manager_ctx=owner,.deleter=release_legacy};
    *out=owner; return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_dlpack_export_copy(const CettaTensor *source,DLManagedTensorVersioned **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL; ExportOwner *owner=NULL; CettaTensorStatus status=export_owner(source,&owner);
    if(status==CETTA_TENSOR_OK) *out=&owner->versioned;
    return status;
}
CettaTensorStatus cetta_tensor_dlpack_export_legacy_copy(const CettaTensor *source,DLManagedTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL; ExportOwner *owner=NULL; CettaTensorStatus status=export_owner(source,&owner);
    if(status==CETTA_TENSOR_OK) *out=&owner->legacy;
    return status;
}
