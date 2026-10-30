#include "tensor.h"
#ifndef CETTA_TENSOR_STANDALONE
#include "space.h"
#include "match.h"
#include "owned_return_image.h"
#endif
#include <assert.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

static unsigned checks;
#define check(x) do { ++checks; assert(x); } while(0)
#define ok(x) check((x)==CETTA_TENSOR_OK)
static double at(const CettaTensor *t,size_t i) {
    bool present; CettaTensorScalar x;
    ok(cetta_tensor_get(t,i,&present,&x)); check(present); return x.f64;
}
static void dense_views(void) {
    double values[]={1,2,3,4,5,6}; size_t shape[]={2,3};
    CettaTensor *t=NULL,*v=NULL,*copy=NULL,*sparse=NULL,*csr=NULL,*csc=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,shape,2,values,sizeof(values),&t));
    ok(cetta_tensor_view(t,(size_t[]){3,2},(ptrdiff_t[]){1,3},2,0,&v));
    check(at(v,1)==4 && at(v,2)==2 && at(v,5)==6);
    ok(cetta_tensor_copy(v,&copy)); check(cetta_tensor_equal(v,copy));
    check(cetta_tensor_hash(v)==cetta_tensor_hash(copy));
    ok(cetta_tensor_convert(v,CETTA_TENSOR_COO,&sparse));
    ok(cetta_tensor_convert(sparse,CETTA_TENSOR_CSR,&csr));
    ok(cetta_tensor_convert(sparse,CETTA_TENSOR_CSC,&csc));
    check(cetta_tensor_equal(v,sparse) && cetta_tensor_equal(copy,csr) && cetta_tensor_equal(csr,csc));
    check(cetta_tensor_hash(csr)==cetta_tensor_hash(copy));
    cetta_tensor_release(copy); cetta_tensor_release(v);
    ok(cetta_tensor_view(t,(size_t[]){6},(ptrdiff_t[]){-1},1,5,&v));
    check(at(v,0)==6 && at(v,5)==1); cetta_tensor_release(v);
    ok(cetta_tensor_view(t,(size_t[]){7},(ptrdiff_t[]){0},1,2,&v));
    cetta_tensor_release(t); check(at(v,0)==3 && at(v,6)==3);
    CettaTensor *bad=NULL;
    check(cetta_tensor_view(v,(size_t[]){8},(ptrdiff_t[]){1},1,0,&bad)==CETTA_TENSOR_INDEX_ERROR);
    check(cetta_tensor_view(v,(size_t[]){2},(ptrdiff_t[]){-1},1,0,&bad)==CETTA_TENSOR_INDEX_ERROR);
    check(cetta_tensor_view(v,(size_t[]){3},(ptrdiff_t[]){PTRDIFF_MAX},1,0,&bad)==CETTA_TENSOR_OVERFLOW);
    unsigned char *wire=NULL; size_t size=0;
    ok(cetta_tensor_encode(v,&wire,&size)); ok(cetta_tensor_decode(wire,size,&copy));
    check(cetta_tensor_equal(v,copy));
    check(cetta_tensor_decode(wire,size-1,&bad)==CETTA_TENSOR_ENCODING);
    check(cetta_tensor_decode(wire,size+1,&bad)==CETTA_TENSOR_ENCODING);
    free(wire); cetta_tensor_release(copy); cetta_tensor_release(v);
    cetta_tensor_release(sparse); cetta_tensor_release(csr); cetta_tensor_release(csc);
    size_t count,bytes;
    ok(cetta_tensor_byte_count(CETTA_TENSOR_F64,(size_t[]){SIZE_MAX,SIZE_MAX,0},3,&count,&bytes));
    check(!count && !bytes);
    check(cetta_tensor_byte_count(CETTA_TENSOR_F64,(size_t[]){SIZE_MAX,2},2,&count,&bytes)==CETTA_TENSOR_OVERFLOW);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,NULL,0,(double[]){2},8,&t));
    check(cetta_tensor_count(t)==1 && at(t,0)==2); cetta_tensor_release(t);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2,0},2,NULL,0,&t));
    ok(cetta_tensor_convert(t,CETTA_TENSOR_CSR,&csr)); check(cetta_tensor_equal(t,csr));
    cetta_tensor_release(t); cetta_tensor_release(csr);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2,3},2,values,sizeof(values),&t));
    ok(cetta_tensor_view(t,(size_t[]){1,3},(ptrdiff_t[]){3,1},2,3,&v));
    ok(cetta_tensor_reshape(v,(size_t[]){3},1,&copy));
    check(at(copy,0)==4 && at(copy,2)==6);
    cetta_tensor_release(copy); cetta_tensor_release(v);
    ok(cetta_tensor_view(t,(size_t[]){3,2},(ptrdiff_t[]){1,3},2,0,&v));
    ok(cetta_tensor_reshape(v,(size_t[]){6},1,&copy));
    check(at(copy,1)==4 && at(copy,4)==3);
    check(cetta_tensor_reshape(v,(size_t[]){7},1,&bad)==CETTA_TENSOR_SHAPE && !bad);
    cetta_tensor_release(copy); cetta_tensor_release(v); cetta_tensor_release(t);
}
static void support_duplicates(void) {
    size_t shape[]={2,3},coordinates[]={3,1,3,1}; double data[]={0,2,4,-2};
    CettaTensor *t=NULL,*first=NULL,*last=NULL,*sum=NULL,*copy=NULL,*zero=NULL;
    check(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,data,4,CETTA_TENSOR_REJECT,&t)==CETTA_TENSOR_DUPLICATE);
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,data,4,CETTA_TENSOR_FIRST,&first));
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,data,4,CETTA_TENSOR_LAST,&last));
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,data,4,CETTA_TENSOR_SUM,&sum));
    check(at(first,3)==0 && at(last,3)==4 && at(sum,1)==0);
    bool present; CettaTensorScalar x;
    ok(cetta_tensor_get(sum,0,&present,&x)); check(!present);
    ok(cetta_tensor_get(sum,1,&present,&x)); check(present && x.f64==0);
    check(cetta_tensor_stored(sum)==2);
    check(cetta_tensor_convert(sum,CETTA_TENSOR_DENSE,&copy)==CETTA_TENSOR_SHAPE);
    unsigned char *wire=NULL; size_t size;
    check(cetta_tensor_encode(sum,&wire,&size)==CETTA_TENSOR_ENCODING && !wire);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,shape,2,(double[]){0,0,0,0,0,0},48,&zero));
    check(!cetta_tensor_equal(sum,zero));
    ok(cetta_tensor_convert(sum,CETTA_TENSOR_CSC,&copy)); check(cetta_tensor_equal(sum,copy));
    cetta_tensor_release(copy); cetta_tensor_release(first); cetta_tensor_release(last);
    cetta_tensor_release(sum); cetta_tensor_release(zero);
    size_t one=1;
    check(cetta_tensor_coo(CETTA_TENSOR_I64,&one,1,(size_t[]){0,0},(int64_t[]){INT64_MAX,1},2,CETTA_TENSOR_SUM,&t)==CETTA_TENSOR_OVERFLOW);
    check(cetta_tensor_new(CETTA_TENSOR_BOOL,&one,1,(uint8_t[]){2},1,&t)==CETTA_TENSOR_VALUE);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,&one,1,(double[]){0},8,&t));
    ok(cetta_tensor_new(CETTA_TENSOR_F64,&one,1,(double[]){-0.0},8,&zero));
    check(!cetta_tensor_equal(t,zero)); cetta_tensor_release(t); cetta_tensor_release(zero);
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,(size_t[]){SIZE_MAX},1,(size_t[]){SIZE_MAX-1},(double[]){2},1,CETTA_TENSOR_REJECT,&t));
    check(cetta_tensor_count(t)==SIZE_MAX && cetta_tensor_stored(t)==1);
    check(at(t,SIZE_MAX-1)==2);
    check(cetta_tensor_convert(t,CETTA_TENSOR_DENSE,&zero)==CETTA_TENSOR_SHAPE);
    cetta_tensor_release(t);
}
static void compressed_observers(void) {
    const size_t coordinates[]={0,1,2,3,5,6,7,12,17}, shape[]={3,6}, n=9;
    const double values[]={1e16,1,-1e16,1,5,2,-0.0,3,4};
    for(unsigned layout=CETTA_TENSOR_CSR;layout<=CETTA_TENSOR_CSC;layout++) {
        CettaTensor *coo=NULL,*packed=NULL,*keys=NULL,*data=NULL,*copy=NULL;
        CettaTensor *normalized=NULL,*reference_normal=NULL,*cast=NULL,*reference_cast=NULL;
        CettaTensor *query=NULL,*products[2]={NULL,NULL},*product=NULL;
        CettaTensorScalar sums[2], observed;
        ok(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,values,n,CETTA_TENSOR_REJECT,&coo));
        uint64_t hash=cetta_tensor_hash(coo);
        size_t coo_bytes=cetta_tensor_retained_bytes(coo);
        for(unsigned schedule=0;schedule<2;schedule++)
            ok(cetta_tensor_sum(coo,(CettaTensorSchedule)schedule,&sums[schedule]));
        ok(cetta_tensor_normalize_rows(coo,&reference_normal));
        ok(cetta_tensor_cast(coo,CETTA_TENSOR_F32,CETTA_TENSOR_CAST_NEAREST,&reference_cast));
        ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){6},1,
            (double[]){1,1,1,1,1,1},48,&query));
        for(unsigned schedule=0;schedule<2;schedule++)
            ok(cetta_tensor_matvec(coo,query,(CettaTensorSchedule)schedule,&products[schedule]));
        check(!cetta_tensor_equal(products[0],products[1]));
        ok(cetta_tensor_convert(coo,(CettaTensorLayout)layout,&packed));
        cetta_tensor_release(coo); /* The compressed owner must stand alone. */
        size_t segments=shape[layout==CETTA_TENSOR_CSR ? 0 : 1]+1;
        size_t extra=segments*sizeof(size_t)+(layout==CETTA_TENSOR_CSC ? n*sizeof(size_t) : 0);
        check(cetta_tensor_retained_bytes(packed)==coo_bytes+extra);
        check(cetta_tensor_hash(packed)==hash);
        ok(cetta_tensor_entries(packed,&keys,&data));
        for(size_t i=0;i<n;i++) {
            bool present;
            ok(cetta_tensor_get(keys,i,&present,&observed));
            check(present && observed.index==coordinates[i]);
            ok(cetta_tensor_get(data,i,&present,&observed));
            check(present && memcmp(&observed.f64,&values[i],8)==0);
        }
        for(unsigned schedule=0;schedule<2;schedule++) {
            ok(cetta_tensor_sum(packed,(CettaTensorSchedule)schedule,&observed));
            check(memcmp(&observed.f64,&sums[schedule].f64,8)==0);
            ok(cetta_tensor_matvec(packed,query,(CettaTensorSchedule)schedule,&product));
            check(cetta_tensor_equal(product,products[schedule]));
            cetta_tensor_release(product); cetta_tensor_release(products[schedule]);
        }
        cetta_tensor_release(query);
        ok(cetta_tensor_copy(packed,&copy));
        check(cetta_tensor_equal(packed,copy)); cetta_tensor_release(copy);
        ok(cetta_tensor_reshape(packed,(size_t[]){18},1,&copy));
        bool present;
        ok(cetta_tensor_get(copy,7,&present,&observed)); check(present && signbit(observed.f64));
        ok(cetta_tensor_get(copy,8,&present,&observed)); check(!present);
        cetta_tensor_release(copy);
        ok(cetta_tensor_normalize_rows(packed,&normalized));
        check(cetta_tensor_equal(normalized,reference_normal));
        ok(cetta_tensor_cast(packed,CETTA_TENSOR_F32,CETTA_TENSOR_CAST_NEAREST,&cast));
        check(cetta_tensor_equal(cast,reference_cast));
        cetta_tensor_release(cast); cetta_tensor_release(reference_cast);
        cetta_tensor_release(normalized); cetta_tensor_release(reference_normal);
        cetta_tensor_release(data); cetta_tensor_release(keys); cetta_tensor_release(packed);
    }
}
static uint32_t random_word=83;
static uint32_t rng(void) { random_word=random_word*1664525u+1013904223u; return random_word; }
static void kernels(void) {
    for(unsigned trial=0;trial<180;trial++) {
        size_t rows=rng()%27,cols=rng()%23,shape[]={rows,cols},entries=0;
        size_t *coordinates=malloc((rows*cols+1)*sizeof(size_t));
        double *values=malloc((rows*cols+1)*sizeof(double)),*query=malloc((cols+1)*sizeof(double));
        double *expected=calloc(rows+1,sizeof(double));
        for(size_t col=0;col<cols;col++) query[col]=(int)(rng()%21)-10;
        for(size_t row=0;row<rows;row++) for(size_t col=0;col<cols;col++) if(rng()%5==0) {
            coordinates[entries]=row*cols+col; values[entries]=(int)(rng()%31)-15;
            expected[row]+=values[entries]*query[col]; entries++;
        }
        CettaTensor *t=NULL,*v=NULL,*result=NULL;
        ok(cetta_tensor_coo(CETTA_TENSOR_F64,shape,2,coordinates,values,entries,CETTA_TENSOR_REJECT,&t));
        ok(cetta_tensor_new(CETTA_TENSOR_F64,&cols,1,query,cols*8,&v));
        for(unsigned layout=1;layout<=3;layout++) {
            CettaTensor *converted=NULL;
            ok(cetta_tensor_convert(t,(CettaTensorLayout)layout,&converted));
            check(cetta_tensor_equal(t,converted));
            ok(cetta_tensor_matvec(converted,v,CETTA_TENSOR_SEQUENTIAL,&result));
            for(size_t row=0;row<rows;row++) check(at(result,row)==expected[row]);
            cetta_tensor_release(result); cetta_tensor_release(converted);
        }
        cetta_tensor_release(v); cetta_tensor_release(t);
        free(coordinates); free(values); free(query); free(expected);
    }
    size_t n=6; double values[]={2,4,4,-1,8,4}; CettaTensor *t=NULL,*mask=NULL,*r=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,&n,1,values,sizeof(values),&t));
    ok(cetta_tensor_top_k(t,NULL,3,&r));
    for(size_t i=0;i<3;i++) { bool p; CettaTensorScalar x; ok(cetta_tensor_get(r,i,&p,&x)); check(p && x.index==((uint64_t[]){4,1,2})[i]); }
    cetta_tensor_release(r);
    ok(cetta_tensor_new(CETTA_TENSOR_BOOL,&n,1,(uint8_t[]){1,1,0,1,0,1},6,&mask));
    ok(cetta_tensor_top_k(t,mask,12,&r)); check(cetta_tensor_count(r)==4);
    cetta_tensor_release(r); cetta_tensor_release(mask); cetta_tensor_release(t);
    n=1; ok(cetta_tensor_new(CETTA_TENSOR_F64,&n,1,(double[]){NAN},8,&t));
    check(cetta_tensor_top_k(t,NULL,0,&r)==CETTA_TENSOR_VALUE); cetta_tensor_release(t);
    n=4; ok(cetta_tensor_new(CETTA_TENSOR_F64,&n,1,(double[]){1e16,1,-1e16,1},32,&t));
    CettaTensorScalar sequential,tree;
    ok(cetta_tensor_sum(t,CETTA_TENSOR_SEQUENTIAL,&sequential));
    ok(cetta_tensor_sum(t,CETTA_TENSOR_FIXED4,&tree)); check(sequential.f64==1);
    cetta_tensor_release(t);
}
static void normalized_and_ordered(void) {
    CettaTensor *t=NULL,*normalized=NULL,*query=NULL,*coo=NULL,*csc=NULL,*a=NULL,*b=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2,2},2,
        (double[]){DBL_MAX,DBL_MAX,DBL_TRUE_MIN,0},32,&t));
    ok(cetta_tensor_normalize_rows(t,&normalized)); check(cetta_tensor_all_finite(normalized));
    double first=at(normalized,0); check(fabs(first-sqrt(.5))<1e-15);
    check(at(normalized,1)==first && at(normalized,2)==1 && at(normalized,3)==0);
    cetta_tensor_release(t); cetta_tensor_release(normalized);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){1},1,(double[]){0},8,&t));
    check(cetta_tensor_normalize_rows(t,&normalized)==CETTA_TENSOR_ZERO_NORM); check(!normalized); cetta_tensor_release(t);
    uint64_t nan_bits=UINT64_C(0x7ff80000000000a7); double nan; memcpy(&nan,&nan_bits,8);
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){1},1,&nan,8,&t));
    check(!cetta_tensor_all_finite(t));
    unsigned char *wire; size_t bytes;
    ok(cetta_tensor_encode(t,&wire,&bytes)); ok(cetta_tensor_decode(wire,bytes,&a));
    check(cetta_tensor_equal(t,a)); check(cetta_tensor_hash(t)==cetta_tensor_hash(a));
    check(cetta_tensor_normalize_rows(t,&normalized)==CETTA_TENSOR_VALUE);
    free(wire); cetta_tensor_release(t); cetta_tensor_release(a);
    double values[]={1e16,1,-1e16,1,1e16,1,-1e16,1};
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){1,8},2,values,sizeof(values),&t));
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){8},1,(double[]){1,1,1,1,1,1,1,1},64,&query));
    ok(cetta_tensor_convert(t,CETTA_TENSOR_COO,&coo)); ok(cetta_tensor_convert(coo,CETTA_TENSOR_CSC,&csc));
    ok(cetta_tensor_matvec(t,query,CETTA_TENSOR_SEQUENTIAL,&a));
    ok(cetta_tensor_matvec(csc,query,CETTA_TENSOR_SEQUENTIAL,&b)); check(cetta_tensor_equal(a,b));
    cetta_tensor_release(b);
    ok(cetta_tensor_matvec(csc,query,CETTA_TENSOR_FIXED4,&b)); check(!cetta_tensor_equal(a,b));
    cetta_tensor_release(a); cetta_tensor_release(b); cetta_tensor_release(csc);
    cetta_tensor_release(coo); cetta_tensor_release(query); cetta_tensor_release(t);
}
static void dense_schedules(void) {
    for(unsigned mt=0;mt<2;mt++) for(unsigned vt=0;vt<2;vt++) for(unsigned trial=0;trial<32;trial++) {
        size_t rows=trial+1,cols=(trial*7)%41,shape[]={rows,cols},n=rows*cols;
        double *matrix=malloc((n+1)*8),*query=malloc((cols+1)*8),*expected=calloc(rows,8);
        float *compact_matrix=malloc((n+1)*4),*compact_query=malloc((cols+1)*4);
        check(matrix && query && expected && compact_matrix && compact_query);
        for(size_t i=0;i<n;i++) {
            matrix[i]=((int)(rng()%100000)-50000)/13.0;
            compact_matrix[i]=(float)matrix[i]; if(mt) matrix[i]=compact_matrix[i];
        }
        for(size_t i=0;i<cols;i++) {
            query[i]=((int)(rng()%100000)-50000)/17.0;
            compact_query[i]=(float)query[i]; if(vt) query[i]=compact_query[i];
        }
        CettaTensor *m=NULL,*v=NULL,*result=NULL,*reference=NULL;
        ok(cetta_tensor_new(mt ? CETTA_TENSOR_F32 : CETTA_TENSOR_F64,shape,2,mt ? (const void *)compact_matrix : matrix,n*(mt ? 4 : 8),&m));
        ok(cetta_tensor_new(vt ? CETTA_TENSOR_F32 : CETTA_TENSOR_F64,&cols,1,vt ? (const void *)compact_query : query,cols*(vt ? 4 : 8),&v));
        for(unsigned policy=0;policy<2;policy++) {
            for(size_t row=0;row<rows;row++) {
                double lanes[4]={0,0,0,0};
                for(size_t col=0;col<cols;col++) { double product=matrix[row*cols+col]*query[col]; lanes[policy ? col%4 : 0]+=product; }
                expected[row]=policy ? ((lanes[0]+lanes[1])+lanes[2])+lanes[3] : lanes[0];
            }
            ok(cetta_tensor_new(CETTA_TENSOR_F64,&rows,1,expected,rows*8,&reference));
            ok(cetta_tensor_matvec(m,v,(CettaTensorSchedule)policy,&result));
            check(cetta_tensor_equal(reference,result)); cetta_tensor_release(reference); cetta_tensor_release(result);
        }
        cetta_tensor_release(m); cetta_tensor_release(v);
        free(matrix); free(query); free(expected); free(compact_matrix); free(compact_query);
    }
}
static void casts(void) {
    uint8_t values[256]; for(size_t i=0;i<256;i++) values[i]=(uint8_t)i;
    CettaTensor *source=NULL,*converted=NULL,*back=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_U8,(size_t[]){256},1,values,256,&source));
    for(unsigned target=1;target<=6;target++) {
        if(!cetta_tensor_exact_promotion(CETTA_TENSOR_U8,(CettaTensorDType)target)) continue;
        ok(cetta_tensor_cast(source,(CettaTensorDType)target,CETTA_TENSOR_CAST_EXACT,&converted));
        ok(cetta_tensor_cast(converted,CETTA_TENSOR_U8,CETTA_TENSOR_CAST_EXACT,&back));
        check(cetta_tensor_equal(source,back)); cetta_tensor_release(converted); cetta_tensor_release(back);
    }
    cetta_tensor_release(source);
    check(!cetta_tensor_exact_promotion(CETTA_TENSOR_I64,CETTA_TENSOR_F64));
    check(!cetta_tensor_exact_promotion(CETTA_TENSOR_INDEX,CETTA_TENSOR_I64));
    for(unsigned i=0;i<6;i++) {
        int64_t value=((int64_t[]){INT64_MIN,INT64_MIN+1,-9007199254740993,0,9007199254740993,INT64_MAX})[i];
        ok(cetta_tensor_new(CETTA_TENSOR_I64,(size_t[]){1},1,&value,8,&source));
        CettaTensorStatus expected=(i==0 || i==3) ? CETTA_TENSOR_OK : CETTA_TENSOR_INEXACT;
        check(cetta_tensor_cast(source,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_EXACT,&converted)==expected);
        if(expected==CETTA_TENSOR_OK) cetta_tensor_release(converted);
        ok(cetta_tensor_cast(source,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_NEAREST,&converted));
        check(at(converted,0)==(double)value);
        cetta_tensor_release(converted); cetta_tensor_release(source);
    }
    for(unsigned i=0;i<1000;i++) {
        uint32_t bits=rng(); float value; memcpy(&value,&bits,4);
        ok(cetta_tensor_new(CETTA_TENSOR_F32,(size_t[]){1},1,&value,4,&source));
        ok(cetta_tensor_cast(source,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_EXACT,&converted));
        ok(cetta_tensor_cast(converted,CETTA_TENSOR_F32,CETTA_TENSOR_CAST_EXACT,&back));
        check(cetta_tensor_equal(source,back));
        cetta_tensor_release(source); cetta_tensor_release(converted); cetta_tensor_release(back);
    }
    double special[]={-0.0,INFINITY,-INFINITY};
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){3},1,special,sizeof(special),&source));
    ok(cetta_tensor_cast(source,CETTA_TENSOR_F32,CETTA_TENSOR_CAST_EXACT,&converted));
    ok(cetta_tensor_cast(converted,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_EXACT,&back));
    check(cetta_tensor_equal(source,back));
    cetta_tensor_release(converted); cetta_tensor_release(back); cetta_tensor_release(source);
    double fractions[]={-1.25,2.9};
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2},1,fractions,16,&source));
    check(cetta_tensor_cast(source,CETTA_TENSOR_I64,CETTA_TENSOR_CAST_EXACT,&converted)==CETTA_TENSOR_INEXACT);
    ok(cetta_tensor_cast(source,CETTA_TENSOR_I64,CETTA_TENSOR_CAST_TRUNCATE,&converted));
    bool present; CettaTensorScalar scalar;
    ok(cetta_tensor_get(converted,0,&present,&scalar)); check(present && scalar.i64==-1);
    ok(cetta_tensor_get(converted,1,&present,&scalar)); check(present && scalar.i64==2);
    cetta_tensor_release(converted); cetta_tensor_release(source);
    uint64_t maximum=UINT64_MAX;
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){1},1,&maximum,8,&source));
    check(cetta_tensor_cast(source,CETTA_TENSOR_I64,CETTA_TENSOR_CAST_EXACT,&converted)==CETTA_TENSOR_OVERFLOW);
    check(cetta_tensor_cast(source,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_EXACT,&converted)==CETTA_TENSOR_INEXACT);
    cetta_tensor_release(source);
    ok(cetta_tensor_coo(CETTA_TENSOR_I64,(size_t[]){2,3},2,(size_t[]){2,4},(int64_t[]){0,7},2,CETTA_TENSOR_REJECT,&source));
    ok(cetta_tensor_convert(source,CETTA_TENSOR_CSC,&converted)); cetta_tensor_release(source); source=converted;
    ok(cetta_tensor_cast(source,CETTA_TENSOR_F64,CETTA_TENSOR_CAST_EXACT,&converted));
    check(cetta_tensor_layout(converted)==CETTA_TENSOR_CSC && cetta_tensor_stored(converted)==2);
    ok(cetta_tensor_cast(converted,CETTA_TENSOR_I64,CETTA_TENSOR_CAST_EXACT,&back));
    check(cetta_tensor_equal(source,back));
    cetta_tensor_release(source); cetta_tensor_release(converted); cetta_tensor_release(back);
    double tenth=.1;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){1},1,&tenth,8,&source));
    int rounding=fegetround(); check(fesetround(FE_DOWNWARD)==0);
    check(cetta_tensor_cast(source,CETTA_TENSOR_F32,CETTA_TENSOR_CAST_NEAREST,&converted)==CETTA_TENSOR_VALUE);
    check(fesetround(rounding)==0); cetta_tensor_release(source);
}
static void packed_observers(void) {
    CettaTensor *source=NULL,*keys=NULL,*values=NULL,*indices=NULL,*gathered=NULL,*merged=NULL;
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,(size_t[]){4},1,
        (size_t[]){1,3},(double[]){-0.0,7.0},2,CETTA_TENSOR_REJECT,&source));
    ok(cetta_tensor_entries(source,&keys,&values));
    check(cetta_tensor_dtype(keys)==CETTA_TENSOR_INDEX);
    check(cetta_tensor_count(keys)==2 && cetta_tensor_count(values)==2);
    bool present; CettaTensorScalar scalar;
    ok(cetta_tensor_get(keys,0,&present,&scalar)); check(present && scalar.index==1);
    ok(cetta_tensor_get(values,0,&present,&scalar)); check(present && signbit(scalar.f64));
    cetta_tensor_release(keys); cetta_tensor_release(values);
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){4},1,
        (uint64_t[]){3,0,1,3},32,&indices));
    ok(cetta_tensor_gather(source,indices,&gathered));
    check(cetta_tensor_stored(gathered)==3);
    ok(cetta_tensor_get(gathered,1,&present,&scalar)); check(!present);
    ok(cetta_tensor_get(gathered,2,&present,&scalar)); check(present && signbit(scalar.f64));
    check(cetta_tensor_gather(source,source,&merged)==CETTA_TENSOR_DTYPE && !merged);
    ok(cetta_tensor_merge(source,source,CETTA_TENSOR_LAST,&merged));
    check(cetta_tensor_equal(merged,source)); cetta_tensor_release(merged);
    check(cetta_tensor_merge(source,source,CETTA_TENSOR_REJECT,&merged)==CETTA_TENSOR_DUPLICATE && !merged);
    ok(cetta_tensor_merge(source,source,CETTA_TENSOR_SUM,&merged));
    check(at(merged,3)==14.0); cetta_tensor_release(merged);
    cetta_tensor_release(source); cetta_tensor_release(indices); cetta_tensor_release(gathered);
    ok(cetta_tensor_new(CETTA_TENSOR_I64,(size_t[]){1},1,(int64_t[]){INT64_MAX},8,&source));
    check(cetta_tensor_merge(source,source,CETTA_TENSOR_SUM,&merged)==CETTA_TENSOR_OVERFLOW && !merged);
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){1},1,(uint64_t[]){UINT64_MAX},8,&indices));
    check(cetta_tensor_gather(source,indices,&merged)==CETTA_TENSOR_INDEX_ERROR && !merged);
    cetta_tensor_release(source); cetta_tensor_release(indices);
}

static void gathered_rows(void) {
    CettaTensor *source=NULL,*rows=NULL,*result=NULL,*converted=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2,3},2,
        (double[]){1,-0.0,3,4,5,6},48,&source));
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){3},1,(uint64_t[]){1,0,1},24,&rows));
    for (CettaTensorLayout layout=CETTA_TENSOR_DENSE;layout<=CETTA_TENSOR_CSC;layout++) {
        ok(cetta_tensor_convert(source,layout,&converted));
        ok(cetta_tensor_gather_rows(converted,rows,&result));
        check(cetta_tensor_rank(result)==2 && cetta_tensor_shape(result)[0]==3 && cetta_tensor_shape(result)[1]==3);
        check(at(result,0)==4 && at(result,3)==1 && signbit(at(result,4)) && at(result,8)==6);
        cetta_tensor_release(converted); cetta_tensor_release(result); result=NULL;
    }
    check(cetta_tensor_gather_rows(source,source,&result)==CETTA_TENSOR_DTYPE && !result);
    cetta_tensor_release(rows); cetta_tensor_release(source);
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,(size_t[]){2,3},2,
        (size_t[]){1,4},(double[]){-0.0,7},2,CETTA_TENSOR_REJECT,&source));
    ok(cetta_tensor_coo(CETTA_TENSOR_INDEX,(size_t[]){4},1,
        (size_t[]){0,2,3},(uint64_t[]){1,0,1},3,CETTA_TENSOR_REJECT,&rows));
    ok(cetta_tensor_gather_rows(source,rows,&result));
    check(cetta_tensor_stored(result)==3 && at(result,1)==7 && signbit(at(result,7)) && at(result,10)==7);
    bool present; CettaTensorScalar scalar;
    ok(cetta_tensor_get(result,4,&present,&scalar)); check(!present);
    cetta_tensor_release(rows); cetta_tensor_release(source); cetta_tensor_release(result); result=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2,0},2,NULL,0,&source));
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){1},1,(uint64_t[]){2},8,&rows));
    check(cetta_tensor_gather_rows(source,rows,&result)==CETTA_TENSOR_INDEX_ERROR && !result);
    cetta_tensor_release(rows);
    ok(cetta_tensor_new(CETTA_TENSOR_INDEX,(size_t[]){1},1,(uint64_t[]){1},8,&rows));
    ok(cetta_tensor_gather_rows(source,rows,&result)); check(cetta_tensor_count(result)==0);
    cetta_tensor_release(rows); cetta_tensor_release(source); cetta_tensor_release(result); result=NULL;
    size_t huge=SIZE_MAX/3;
    ok(cetta_tensor_coo(CETTA_TENSOR_F64,(size_t[]){1,3},2,
        (size_t[]){2},(double[]){9},1,CETTA_TENSOR_REJECT,&source));
    ok(cetta_tensor_coo(CETTA_TENSOR_INDEX,&huge,1,
        (size_t[]){huge-1},(uint64_t[]){0},1,CETTA_TENSOR_REJECT,&rows));
    ok(cetta_tensor_gather_rows(source,rows,&result));
    check(cetta_tensor_stored(result)==1 && at(result,(huge-1)*3+2)==9);
    cetta_tensor_release(rows); cetta_tensor_release(source); cetta_tensor_release(result);
}

static void owned_atoms(void) {
    Arena source,destination,persistent; arena_init(&source); arena_init(&destination); arena_init(&persistent);
    CettaTensor *dense=NULL,*coo=NULL;
    ok(cetta_tensor_new(CETTA_TENSOR_F64,(size_t[]){2},1,(double[]){3,0},16,&dense));
    ok(cetta_tensor_convert(dense,CETTA_TENSOR_COO,&coo));
    Atom *a=cetta_tensor_atom(&source,dense),*b=cetta_tensor_atom(&source,coo);
    cetta_tensor_release(dense); cetta_tensor_release(coo);
    check(atom_eq(a,b)); check(atom_hash(a)==atom_hash(b));
    check(atom_structural_compare(a,b)==0); check(!strcmp(atom_structural_type_name(a),"Tensor"));
    check(atom_structural_retained_bytes(a)>=16);
    Atom *copy=atom_deep_copy(&destination,a);
    size_t charge=destination.external_bytes;
    check(charge>=atom_structural_retained_bytes(copy));
    check(atom_eq(atom_deep_copy(&destination,a),copy));
    check(destination.external_bytes==charge);
    Atom *second=atom_deep_copy(&persistent,b);
#ifndef CETTA_TENSOR_STANDALONE
    TermUniverse universe; term_universe_init(&universe); term_universe_set_persistent_arena(&universe,&persistent);
    Atom *stored=term_universe_store_atom(&universe,&persistent,b); check(stored && atom_eq(copy,stored));
    Atom *pair=atom_expr2(&source,a,b);
    Atom *x=atom_var(&source,"x");
    Bindings bindings; bindings_init(&bindings);
    check(match_atoms(atom_expr2(&source,x,x),pair,&bindings,&destination)); bindings_free(&bindings);
    CettaOwnedReturn returned={.outcome=cetta_call_completed_value(pair)};
    CettaOwnedReturnImage *image=cetta_owned_return_image_capture(&returned,NULL); check(image);
    arena_free(&source); check(atom_eq(copy,stored));
    const CettaOwnedReturn *view=cetta_owned_return_image_view(image);
    check(atom_eq(view->outcome.term->expr.elems[0],copy));
#else
    arena_free(&source);
#endif
    check(atom_eq(copy,second));
    Atom *inspection=atom_structural_inspect(copy,&destination);
    check(inspection && atom_is_symbol(inspection->expr.elems[0],"TensorData"));
#ifndef CETTA_TENSOR_STANDALONE
    cetta_owned_return_image_release(image); term_universe_free(&universe);
#endif
    arena_free(&persistent); arena_free(&destination);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    dense_views(); support_duplicates(); compressed_observers(); kernels(); normalized_and_ordered(); dense_schedules(); casts(); packed_observers(); gathered_rows(); owned_atoms();
    symbol_table_free(&symbols); g_symbols=NULL;
    printf("TensorNative %u 0\n",checks); return 0;
}
