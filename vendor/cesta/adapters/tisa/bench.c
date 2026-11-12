#define _POSIX_C_SOURCE 200809L
#include "mixed_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void copy_span_failure(void) { abort(); }
void prefix_length_failure(void) { abort(); }

static size_t ordinary_copy(TcIsaRuntimeFrame *unused,
                             cesta_read_u32_span input,
                             cesta_write_u32_span output) {
    (void)unused;
    size_t n=prefix_length_count(input.len,output.len);
    copy_span_copy_u32((cesta_read_u32_span){input.data,n},output);
    return n;
}

typedef size_t (*Pipeline)(TcIsaRuntimeFrame *,cesta_read_u32_span,cesta_write_u32_span);

static double measure(Pipeline operation,unsigned iterations,uint64_t *receipt) {
    uint32_t source[64],destination[64]={0};
    for(unsigned i=0;i<64;++i)source[i]=UINT32_C(0xf1234567)+i*97u;
    TcIsaRuntimeFrame frame={0};
    struct timespec begin,end;
    if(clock_gettime(CLOCK_MONOTONIC,&begin))abort();
    uint64_t sum=0;
    for(unsigned i=0;i<iterations;++i) {
        size_t n=(i*37u)%65u,m=(i*19u)%65u;
        size_t count=operation(&frame,(cesta_read_u32_span){source,n},
                                      (cesta_write_u32_span){destination,m});
        sum+=count;
        if(count)sum^=destination[count-1];
    }
    if(clock_gettime(CLOCK_MONOTONIC,&end))abort();
    *receipt=sum;
    return (1e9*(double)(end.tv_sec-begin.tv_sec)+(double)(end.tv_nsec-begin.tv_nsec))/iterations;
}

int main(void) {
    uint64_t ordinary,mixed;
    (void)measure(ordinary_copy,20000,&ordinary);
    (void)measure(mixed_kernel_copy,20000,&mixed);
    if(ordinary!=mixed)return 1;
    puts("ordinary_ns,mixed_ns,checksum");
    for(unsigned trial=0;trial<7;++trial) {
        double a,b;
        if(trial&1u) {
            b=measure(mixed_kernel_copy,500000,&mixed);
            a=measure(ordinary_copy,500000,&ordinary);
        } else {
            a=measure(ordinary_copy,500000,&ordinary);
            b=measure(mixed_kernel_copy,500000,&mixed);
        }
        if(ordinary!=mixed)return 2;
        printf("%.6f,%.6f,%llu\n",a,b,(unsigned long long)ordinary);
    }
    return 0;
}
