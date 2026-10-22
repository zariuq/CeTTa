#define _POSIX_C_SOURCE 200809L
#include "tensor.h"
#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

static uint64_t clock_ns(clockid_t clock) {
    struct timespec t; if(clock_gettime(clock,&t)) abort();
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static size_t natural(const char *text) {
    char *end=NULL; errno=0; unsigned long long x=strtoull(text,&end,10);
    if(errno || !*text || text[0]=='-' || !end || *end || x>SIZE_MAX) abort();
    return (size_t)x;
}
static void require(CettaTensorStatus status) {
    if(status!=CETTA_TENSOR_OK) { fprintf(stderr,"tensor benchmark: %s\n",cetta_tensor_status_name(status)); exit(1); }
}
static int compare_u64(const void *a,const void *b) { uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return x<y ? -1 : x!=y; }
static double coefficient(size_t row,size_t col) { return ((int)((row%17*13+col%17*7)%17)-8)/16.0; }
static double query_value(size_t col) { return ((int)(col%7)-3)/8.0; }
static long exec_peak_kib(void) {
#ifdef __linux__
    FILE *status=fopen("/proc/self/status","r");
    if(status) {
        char line[256]; long value=0;
        while(fgets(line,sizeof(line),status))
            if(sscanf(line,"VmHWM: %ld kB",&value)==1) { fclose(status); return value; }
        fclose(status);
    }
#endif
    return -1;
}
static void write_tensor(const CettaTensor *tensor,const char *path) {
    unsigned char *bytes=NULL; size_t length=0; require(cetta_tensor_encode(tensor,&bytes,&length));
    FILE *file=fopen(path,"wb"); if(!file || fwrite(bytes,1,length,file)!=length || fclose(file)) abort(); free(bytes);
}
static double read_double(const CettaTensor *tensor,size_t index) {
    bool present; CettaTensorScalar scalar;
    require(cetta_tensor_get(tensor,index,&present,&scalar)); if(!present) abort(); return scalar.f64;
}
int main(int argc,char **argv) {
    if(argc!=8) { fprintf(stderr,"usage: bench_tensor dense|coo|csr|csc f64|f32 rows cols stored-per-row repetitions scores.ctn\n"); return 2; }
    bool dense=!strcmp(argv[1],"dense"); CettaTensorLayout layout=CETTA_TENSOR_COO;
    if(!dense && strcmp(argv[1],"coo") && strcmp(argv[1],"csr") && strcmp(argv[1],"csc")) return 2;
    if(strcmp(argv[2],"f64") && strcmp(argv[2],"f32")) return 2;
    if(!dense && strcmp(argv[1],"coo")) layout=!strcmp(argv[1],"csr") ? CETTA_TENSOR_CSR : CETTA_TENSOR_CSC;
    CettaTensorDType dtype=!strcmp(argv[2],"f32") ? CETTA_TENSOR_F32 : CETTA_TENSOR_F64;
    size_t rows=natural(argv[3]),cols=natural(argv[4]),per_row=natural(argv[5]),repetitions=natural(argv[6]);
    if(!repetitions || per_row>cols || repetitions>SIZE_MAX/sizeof(uint64_t)) return 2;
    size_t shape[]={rows,cols},count,bytes,width=cetta_tensor_itemsize(dtype);
    require(cetta_tensor_coordinate_count(shape,2,&count));
    size_t stored=count;
    if(!dense && __builtin_mul_overflow(rows,per_row,&stored)) return 2;
    if(stored>SIZE_MAX/width || stored>SIZE_MAX/sizeof(size_t) || cols>SIZE_MAX/sizeof(double)) return 2;
    bytes=stored*width;
    uint64_t load_start=clock_ns(CLOCK_MONOTONIC);
    unsigned char *values=bytes ? malloc(bytes) : NULL;
    size_t *coordinates=!dense && stored ? malloc(stored*sizeof(size_t)) : NULL;
    double *q=cols ? malloc(cols*sizeof(double)) : NULL;
    if((bytes && !values) || (!dense && stored && !coordinates) || (cols && !q)) return 1;
    for(size_t col=0;col<cols;col++) q[col]=query_value(col);
    size_t next=0;
    for(size_t row=0;row<rows;row++) for(size_t j=0;j<(dense ? cols : per_row);j++) {
        size_t col=dense ? j : (cols/per_row)*j;
        double x=coefficient(row,col);
        if(dtype==CETTA_TENSOR_F64) memcpy(values+next*width,&x,width);
        else { float compact=(float)x; memcpy(values+next*width,&compact,width); }
        if(coordinates) coordinates[next]=row*cols+col;
        next++;
    }
    uint64_t loaded=clock_ns(CLOCK_MONOTONIC);
    CettaTensor *matrix=NULL,*query=NULL,*scores=NULL;
    if(dense) require(cetta_tensor_new(dtype,shape,2,values,bytes,&matrix));
    else {
        CettaTensor *coo=NULL; require(cetta_tensor_coo(dtype,shape,2,coordinates,values,stored,CETTA_TENSOR_REJECT,&coo));
        require(cetta_tensor_convert(coo,layout,&matrix)); cetta_tensor_release(coo);
    }
    require(cetta_tensor_new(CETTA_TENSOR_F64,&cols,1,q,cols*sizeof(double),&query));
    free(values); free(coordinates); free(q);
    uint64_t prepared=clock_ns(CLOCK_MONOTONIC);
    uint64_t *times=malloc(repetitions*sizeof(*times)),*cpu=malloc(repetitions*sizeof(*cpu));
    if(!times || !cpu) return 1;
    size_t iterations=count ? 1000000/count : 1; if(!iterations) iterations=1;
    uint64_t first_start=clock_ns(CLOCK_MONOTONIC);
    require(cetta_tensor_matvec(matrix,query,CETTA_TENSOR_SEQUENTIAL,&scores));
    uint64_t first_ns=clock_ns(CLOCK_MONOTONIC)-first_start;
    for(size_t row=0;row<rows;row++) {
        double expected=0;
        for(size_t j=0;j<(dense ? cols : per_row);j++) {
            size_t col=dense ? j : (cols/per_row)*j;
            expected+=coefficient(row,col)*query_value(col);
        }
        if(read_double(scores,row)!=expected) { fprintf(stderr,"unmatched row %zu\n",row); return 1; }
    }
    CettaTensor *fixed=NULL;
    require(cetta_tensor_matvec(matrix,query,CETTA_TENSOR_FIXED4,&fixed));
    for(size_t row=0;row<rows;row++) if(read_double(fixed,row)!=read_double(scores,row)) {
        fprintf(stderr,"unmatched fixed4 dyadic row %zu\n",row); return 1;
    }
    cetta_tensor_release(fixed);
    uint64_t verify_ns=clock_ns(CLOCK_MONOTONIC)-first_start-first_ns;
    printf("{\"layout\":\"%s\",\"dtype\":\"%s\",\"rows\":%zu,\"cols\":%zu,\"stored\":%zu,\"iterations\":%zu,",
        argv[1],cetta_tensor_dtype_name(dtype),rows,cols,stored,iterations);
    printf("\"load_ns\":%" PRIu64 ",\"prepare_ns\":%" PRIu64 ",\"first_ns\":%" PRIu64 ",\"verify_ns\":%" PRIu64 ",",loaded-load_start,prepared-loaded,first_ns,verify_ns);
    volatile size_t observed=0;
    for(unsigned schedule=0;schedule<2;schedule++) {
        for(size_t rep=0;rep<repetitions;rep++) {
            uint64_t start=clock_ns(CLOCK_MONOTONIC),cstart=clock_ns(CLOCK_PROCESS_CPUTIME_ID);
            for(size_t i=0;i<iterations;i++) {
                CettaTensor *result=NULL; require(cetta_tensor_matvec(matrix,query,(CettaTensorSchedule)schedule,&result));
                observed+=cetta_tensor_count(result); cetta_tensor_release(result);
            }
            cpu[rep]=(clock_ns(CLOCK_PROCESS_CPUTIME_ID)-cstart)/iterations;
            times[rep]=(clock_ns(CLOCK_MONOTONIC)-start)/iterations;
        }
        qsort(times,repetitions,sizeof(*times),compare_u64); qsort(cpu,repetitions,sizeof(*cpu),compare_u64);
        printf("\"%s_ns\":%" PRIu64 ",\"%s_cpu_ns\":%" PRIu64 ",",schedule ? "fixed4" : "sequential",times[repetitions/2],schedule ? "fixed4" : "sequential",cpu[repetitions/2]);
    }
    size_t k=rows<32 ? rows : 32;
    CettaTensor *top=NULL;
    for(size_t rep=0;rep<repetitions;rep++) {
        uint64_t start=clock_ns(CLOCK_MONOTONIC);
        require(cetta_tensor_top_k(scores,NULL,k,&top));
        times[rep]=clock_ns(CLOCK_MONOTONIC)-start;
        if(rep+1<repetitions) { cetta_tensor_release(top); top=NULL; }
    }
    qsort(times,repetitions,sizeof(*times),compare_u64);
    printf("\"top_k_ns\":%" PRIu64 ",\"top_k\":[",times[repetitions/2]);
    for(size_t i=0;i<k;i++) {
        bool present; CettaTensorScalar scalar; require(cetta_tensor_get(top,i,&present,&scalar)); if(!present) abort();
        printf("%s%" PRIu64,i ? "," : "",scalar.index);
    }
    struct rusage usage; if(getrusage(RUSAGE_SELF,&usage)) abort();
    printf("],\"retained_bytes\":%zu,\"peak_rss_kib\":%ld,\"rusage_peak_rss_kib\":%ld,\"observed\":%zu}\n",
        cetta_tensor_retained_bytes(matrix)+cetta_tensor_retained_bytes(query)+cetta_tensor_retained_bytes(scores),exec_peak_kib(),usage.ru_maxrss,(size_t)observed);
    write_tensor(scores,argv[7]);
    cetta_tensor_release(top); cetta_tensor_release(scores); cetta_tensor_release(query); cetta_tensor_release(matrix);
    free(times); free(cpu); return 0;
}
