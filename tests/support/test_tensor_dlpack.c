#include "tensor_dlpack.h"
#include <assert.h>
#include <stdlib.h>

static unsigned checks,deleted;
#define check(x) do { ++checks; assert(x); } while(0)
#define ok(x) check((x)==CETTA_TENSOR_OK)
static void legacy_deleted(DLManagedTensor *value) { (void)value; deleted++; }
static void versioned_deleted(DLManagedTensorVersioned *value) { (void)value; deleted++; }

int main(void) {
    CettaTensor *tensor=NULL,*back=NULL;
    double values[]={1,2,3,4,5,6}; int64_t shape[]={2,3},strides[]={3,-1};
    DLTensor view={.data=values,.device={kDLCPU,0},.ndim=2,.dtype={kDLFloat,64,1},
        .shape=shape,.strides=strides,.byte_offset=16};
    ok(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor));
    double observed[6]; ok(cetta_tensor_copy_values(tensor,observed,sizeof(observed)));
    check(!memcmp(observed,(double[]){3,2,1,6,5,4},sizeof(observed)));
    values[0]=99;
    ok(cetta_tensor_copy_values(tensor,observed,sizeof(observed))); check(observed[2]==1);
    DLManagedTensorVersioned *exported=NULL;
    ok(cetta_tensor_dlpack_export_copy(tensor,&exported));
    check(exported && exported->version.major==1 && (exported->flags&DLPACK_FLAG_BITMASK_IS_COPIED));
    check(exported->dl_tensor.strides && exported->dl_tensor.strides[0]==3 && exported->dl_tensor.strides[1]==1);
    ((double *)exported->dl_tensor.data)[0]=88;
    ok(cetta_tensor_copy_values(tensor,observed,sizeof(observed))); check(observed[0]==3);
    ok(cetta_tensor_dlpack_consume_versioned(exported,exported->dl_tensor.data,48,&back));
    ok(cetta_tensor_copy_values(back,observed,sizeof(observed))); check(observed[0]==88);
    cetta_tensor_release(back); cetta_tensor_release(tensor);

    view.byte_offset=0;
    check(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor)==CETTA_TENSOR_INDEX_ERROR && !tensor);
    view.strides=(int64_t[]){3,1}; view.byte_offset=0;
    check(cetta_tensor_dlpack_copy(&view,values,sizeof(values)-1,&tensor)==CETTA_TENSOR_INDEX_ERROR);
    view.device.device_type=kDLCUDA;
    check(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor)==CETTA_TENSOR_VALUE);
    view.device.device_type=kDLCPU; view.dtype.lanes=4;
    check(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor)==CETTA_TENSOR_DTYPE);
    view.dtype=(DLDataType){kDLFloat,64,1}; view.strides=(int64_t[]){INT64_MIN,1};
    check(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor)==CETTA_TENSOR_OVERFLOW);
    view.strides=NULL;
    ok(cetta_tensor_dlpack_copy(&view,values,sizeof(values),&tensor)); cetta_tensor_release(tensor);
    DLManagedTensor legacy={.dl_tensor=view,.deleter=legacy_deleted};
    ok(cetta_tensor_dlpack_consume(&legacy,values,sizeof(values),&tensor)); check(deleted==1);
    cetta_tensor_release(tensor);
    DLManagedTensorVersioned invalid={.version={2,0},.deleter=versioned_deleted};
    check(cetta_tensor_dlpack_consume_versioned(&invalid,NULL,0,&tensor)==CETTA_TENSOR_VALUE && deleted==2);
    invalid.version=(DLPackVersion){1,2}; invalid.dl_tensor=view;
    check(cetta_tensor_dlpack_consume_versioned(&invalid,values,sizeof(values),&tensor)==CETTA_TENSOR_VALUE && deleted==3);
    invalid.version.minor=0; invalid.flags=UINT64_MAX;
    check(cetta_tensor_dlpack_consume_versioned(&invalid,values,sizeof(values),&tensor)==CETTA_TENSOR_VALUE && deleted==4);
    view.ndim=2; view.shape=(int64_t[]){0,4}; view.data=NULL;
    ok(cetta_tensor_dlpack_copy(&view,NULL,0,&tensor)); check(cetta_tensor_count(tensor)==0); cetta_tensor_release(tensor);
    view.ndim=0; view.shape=NULL; view.strides=NULL; view.data=values;
    ok(cetta_tensor_dlpack_copy(&view,values,8,&tensor)); check(cetta_tensor_count(tensor)==1);
    DLManagedTensor *old=NULL;
    ok(cetta_tensor_dlpack_export_legacy_copy(tensor,&old));
    cetta_tensor_release(tensor); check(((double *)old->dl_tensor.data)[0]==99); old->deleter(old);
    size_t coord=0; double zero=0;
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,(size_t[]){1},1,&coord,&zero,1,CETTA_TENSOR_REJECT,&tensor));
    check(cetta_tensor_dlpack_export_copy(tensor,&exported)==CETTA_TENSOR_ENCODING && !exported);
    cetta_tensor_release(tensor);
    printf("TensorDLPack %u 0\n",checks);
    return 0;
}
