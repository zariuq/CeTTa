#define _POSIX_C_SOURCE 200809L
#include "tensor_dense_v1.h"
#include "tests/test_runtime_stats_stubs.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

static uint64_t random_state;
static double draw(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return ((double)(random_state >> 11) * 0x1p-53 - .5) * .125;
}
static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return t.tv_sec + t.tv_nsec*1e-9;
}
static void require(CettaDenseTensorV1Status status) {
    if (status != CETTA_DENSE_TENSOR_V1_OK) {
        fprintf(stderr, "tensor benchmark: %s\n", cetta_dense_tensor_v1_status_name(status)); exit(1);
    }
}
static size_t number(const char *text) {
    char *end; unsigned long long n = strtoull(text, &end, 10);
    if (!*text || *end || n > SIZE_MAX || text[0] == '-') exit(2);
    return (size_t)n;
}

int main(int argc, char **argv) {
    if (argc != 6) { fputs("usage: screening ROWS DIMENSION REPEATS K SEED\n", stderr); return 2; }
    size_t n=number(argv[1]), d=number(argv[2]), repeats=number(argv[3]), k=number(argv[4]);
    random_state = number(argv[5]);
    if (!n || !d || !repeats || !k || k>n || !random_state || n>SIZE_MAX/d ||
        n*d>SIZE_MAX/sizeof(double) || d>SIZE_MAX/sizeof(double)) return 2;
    double *data = malloc(n*d*sizeof(double)), *q = malloc(d*sizeof(double));
    if (!data || !q) return 1;
    for (size_t i=0;i<n*d;++i) data[i]=draw();
    size_t shape[]={n,d}; CettaDenseTensorV1 *wide=NULL, *compact=NULL, *certificate=NULL;
    require(cetta_dense_tensor_v1_new(CETTA_DENSE_TENSOR_V1_F64,shape,2,data,n*d*sizeof(double),&wide));
    free(data);
    double build_start=now();
    require(cetta_dense_tensor_v1_cast(wide,CETTA_DENSE_TENSOR_V1_F32,&compact));
    require(cetta_dense_tensor_v1_row_error_bounds(wide,compact,&certificate));
    double build_seconds=now()-build_start;
    printf("{\"rows\":%zu,\"dimension\":%zu,\"index_build_seconds\":%.9g,\"samples\":[",n,d,build_seconds);
    for (size_t iteration=0;iteration<=repeats;++iteration) {
        for (size_t j=0;j<d;++j) q[j]=draw();
        CettaDenseTensorV1 *query=NULL;
        require(cetta_dense_tensor_v1_new(CETTA_DENSE_TENSOR_V1_F64,&d,1,q,d*sizeof(double),&query));
        CettaDenseTensorV1 *scores=NULL, *target=NULL, *lo=NULL, *hi=NULL, *candidates=NULL;
        CettaDenseTensorV1 *selected=NULL, *top=NULL, *mapped=NULL;
        double wide_ms=0, screen_ms=0;
        /* Alternation avoids consistently warming one tier with the other. */
        for (unsigned pass=0;pass<2;++pass) {
            bool screening=(pass+(iteration%2))%2;
            double start=now();
            if (screening) {
                require(cetta_dense_tensor_v1_matvec_bounds(compact,query,certificate,NULL,1,&lo,&hi));
                require(cetta_dense_tensor_v1_top_k_candidates(lo,hi,k,&candidates));
                require(cetta_dense_tensor_v1_matvec(wide,query,candidates,&selected));
                require(cetta_dense_tensor_v1_top_k(selected,NULL,k,&top));
                require(cetta_dense_tensor_v1_gather(candidates,top,&mapped));
                screen_ms=(now()-start)*1000;
            } else {
                require(cetta_dense_tensor_v1_matvec(wide,query,NULL,&scores));
                require(cetta_dense_tensor_v1_top_k(scores,NULL,k,&target));
                wide_ms=(now()-start)*1000;
            }
        }
        if (!cetta_dense_tensor_v1_equal(target,mapped)) { fputs("screening lost exact top-k\n",stderr); return 1; }
        const double *s=cetta_dense_tensor_v1_data(scores), *l=cetta_dense_tensor_v1_data(lo), *h=cetta_dense_tensor_v1_data(hi);
        for (size_t i=0;i<n;++i) if (!(l[i]<=s[i] && s[i]<=h[i])) {
            fputs("invalid score interval\n",stderr); return 1;
        }
        /* A separate long-double scalar reference checks the actual kernel's
         * selected scores. It does not use the four-lane reduction. */
        const double *matrix=cetta_dense_tensor_v1_data(wide); const int64_t *ids=cetta_dense_tensor_v1_data(target);
        for (size_t pos=0;pos<k;++pos) {
            size_t row=(size_t)ids[pos]; long double reference=0,magnitude=0;
            for (size_t j=0;j<d;++j) {
                long double item=(long double)matrix[row*d+j]*q[j];
                reference+=item; magnitude+=fabsl(item);
            }
            if (fabsl(s[row]-reference)>1e-12L*(1+magnitude)) return 1;
        }
        if (iteration) printf("%s{\"wide_ms\":%.9g,\"screen_ms\":%.9g,\"candidates\":%zu}",
            iteration>1 ? "," : "",wide_ms,screen_ms,cetta_dense_tensor_v1_count(candidates));
        cetta_dense_tensor_v1_release(query); cetta_dense_tensor_v1_release(scores); cetta_dense_tensor_v1_release(target);
        cetta_dense_tensor_v1_release(lo); cetta_dense_tensor_v1_release(hi); cetta_dense_tensor_v1_release(candidates);
        cetta_dense_tensor_v1_release(selected); cetta_dense_tensor_v1_release(top); cetta_dense_tensor_v1_release(mapped);
    }
    printf("],\"matrix_bytes\":%zu,\"compact_bytes\":%zu,\"certificate_bytes\":%zu}\n",
        cetta_dense_tensor_v1_nbytes(wide),cetta_dense_tensor_v1_nbytes(compact),cetta_dense_tensor_v1_nbytes(certificate));
    cetta_dense_tensor_v1_release(wide); cetta_dense_tensor_v1_release(compact); cetta_dense_tensor_v1_release(certificate); free(q);
    return 0;
}
