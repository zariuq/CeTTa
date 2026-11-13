#define _GNU_SOURCE
#include "copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef void (*Copy)(cesta_read_u32_span, cesta_write_u32_span);

static int exercise(Copy copy) {
    unsigned cases = 0;
    copy((cesta_read_u32_span){NULL,0}, (cesta_write_u32_span){NULL,0});
    for (size_t count=0;count<=64;count++) {
        for (size_t extra=0;extra<=8;extra++) {
            uint32_t source[66],destination[74],before[74];
            for (size_t i=0;i<66;i++) source[i]=(uint32_t)(i*0x9e3779b9u+count*17u);
            for (size_t i=0;i<74;i++) destination[i]=(uint32_t)(0xa5a5a5a5u+i);
            memcpy(before,destination,sizeof before);
            copy((cesta_read_u32_span){source+1,count},
                 (cesta_write_u32_span){destination+1,count+extra});
            for (size_t i=0;i<count;i++) if(destination[i+1]!=source[i+1])return 1;
            for (size_t i=0;i<74;i++)
                if((i==0||i>count)&&destination[i]!=before[i])return 2;
            for (size_t i=0;i<66;i++)
                if(source[i]!=(uint32_t)(i*0x9e3779b9u+count*17u))return 3;
            ++cases;
        }
    }
    printf("copy: %u prefix/tail/source controls and null-empty call passed\n",cases);
    return 0;
}

#ifdef CESTA_PILOT_NATIVE
void copy_span_failure(void) { abort(); }
int main(void) { return exercise(copy_span_copy_u32); }
#else
/* Execute the exact linked .text bytes. ASan qualification separately uses
   the generated C; instrumentation cannot observe raw instruction accesses. */
int main(int argc, char **argv) {
    if(argc!=2)return 10;
    FILE *file=fopen(argv[1],"rb");if(!file)return 11;
    if(fseek(file,0,SEEK_END))return 12;
    long length=ftell(file);if(length<=0||fseek(file,0,SEEK_SET))return 13;
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return 14;
    size_t extent=((size_t)length+(size_t)page-1)/(size_t)page*(size_t)page;
    void *memory=mmap(NULL,extent,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(memory==MAP_FAILED)return 15;
    if(fread(memory,1,(size_t)length,file)!=(size_t)length||fclose(file))return 16;
    if(mprotect(memory,extent,PROT_READ|PROT_EXEC))return 17;
    Copy copy;_Static_assert(sizeof copy==sizeof memory,"x86 function pointer ABI");
    memcpy(&copy,&memory,sizeof copy);
    int result=exercise(copy);
    if(munmap(memory,extent))return 18;
    return result;
}
#endif
