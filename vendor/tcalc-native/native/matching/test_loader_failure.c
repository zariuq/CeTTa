#include "native_match.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static bool refuse;
static unsigned failures;
void *__real_realloc(void *pointer, size_t bytes);
void *__wrap_realloc(void *pointer, size_t bytes) {
    if (refuse) { refuse=false; ++failures; return NULL; }
    return __real_realloc(pointer,bytes);
}

int main(int argc, char **argv) {
    assert(argc==2);
    char error[256];
    refuse=true;
    assert(!nm_unify_program_open(argv[1],false,error,sizeof error));
    assert(failures==1 && error[0]);
    NmUnifyProgram *program=nm_unify_program_open(argv[1],false,error,sizeof error);
    assert(program);
    nm_unify_program_close(program);
    puts("native matching loader: allocation failure refused cleanly; next load succeeds");
    return 0;
}
