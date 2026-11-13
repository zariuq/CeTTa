#include "mixed_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void copy_span_failure(void) { abort(); }
void prefix_length_failure(void) { abort(); }
void mixed_copy_failure(void) { abort(); }

size_t mixed_copy_count(size_t source, size_t capacity) {
    TcIsaRuntimeFrame frame={0};
    size_t n=(size_t)prefix_length_tisa(&frame,source,capacity);
    if (frame.status!=TISA_OUTCOME_FIRED) abort();
    return n;
}

static uint64_t random_state = UINT64_C(0x9876543210abcdef);
static uint64_t random_word(void) {
    random_state ^= random_state<<13;
    random_state ^= random_state>>7;
    random_state ^= random_state<<17;
    return random_state;
}
int main(void) {
    static const uint64_t edge[] = {0,1,2,UINT32_MAX,UINT64_C(1)<<32,
        (UINT64_C(1)<<63)-1,UINT64_C(1)<<63,UINT64_MAX-1,UINT64_MAX};
    size_t scalar_cases=0, copy_cases=0;
    for (size_t i=0; i<10081; ++i) {
        uint64_t a=i<81 ? edge[i/9] : random_word();
        uint64_t b=i<81 ? edge[i%9] : random_word();
        TcIsaRuntimeFrame frame={0}; frame.fuel_used=UINT64_MAX;
        uint64_t n=prefix_length_tisa(&frame,a,b);
        uint64_t reference=prefix_length_count(a,b);
        if (n!=reference || n!=(a<b?a:b) || frame.ret_val!=n ||
            frame.status!=TISA_OUTCOME_FIRED || frame.fuel_used!=2 || frame.fuel_budget) return 1;
        ++scalar_cases;
    }
    for (size_t n=0; n<=64; ++n) for (size_t m=0; m<=64; ++m) {
        uint32_t source[66], destination[66], checked[66];
        for (size_t j=0; j<66; ++j) {
            source[j]=(uint32_t)random_word();
            destination[j]=checked[j]=UINT32_C(0xa5b6c7d8);
        }
        uint32_t before[66]; memcpy(before,source,sizeof(before));
        TcIsaRuntimeFrame frame={0};
        cesta_read_u32_span input={n?source+1:NULL,n};
        cesta_write_u32_span output={m?destination+1:NULL,m};
        size_t count=mixed_kernel_copy(&frame,input,output), expected=n<m?n:m;
        size_t other=mixed_copy_copy(input,(cesta_write_u32_span){m?checked+1:NULL,m});
        if (count!=expected || frame.status!=TISA_OUTCOME_FIRED || frame.ret_val!=count ||
            frame.fuel_used!=2 || other!=expected || memcmp(before,source,sizeof(before))) return 2;
        for (size_t j=0; j<66; ++j) {
            uint32_t value=j && j<=count ? source[j] : UINT32_C(0xa5b6c7d8);
            if (destination[j]!=value || checked[j]!=value) return 3;
        }
        ++copy_cases;
    }
    /* Retain the incumbent fuel outcomes, including the completed prefix. */
    for (uint64_t budget=1; budget<=3; ++budget) {
        TcIsaRuntimeFrame frame={0}; frame.fuel_budget=budget;
        uint32_t source[4]={1,2,3,4}, destination[4]={9,9,9,9};
        size_t count=mixed_kernel_copy(&frame,(cesta_read_u32_span){source,4},
            (cesta_write_u32_span){destination,4});
        if (budget==1) {
            if (count!=SIZE_MAX || frame.status!=TISA_OUTCOME_EXHAUSTED ||
                frame.ret_val!=TISA_OUTCOME_EXHAUSTED || frame.fuel_used!=1 || destination[0]!=9) return 4;
        } else if (count!=4 || frame.status!=TISA_OUTCOME_FIRED || frame.fuel_used!=2 || destination[3]!=4) return 5;
    }
    printf("mixed leaves: %zu scalar cases, %zu paired prefix copies, fuel/refusal/frame controls\n",scalar_cases,copy_cases);
    return 0;
}
