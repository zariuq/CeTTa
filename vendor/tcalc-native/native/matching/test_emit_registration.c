#include "native_emit.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    char error[256];NmEmitProgram *p=nm_emit_program_default(true,error,sizeof error);
    if (p) { nm_emit_program_close(p);return 1; }
    if (!strstr(error,"does not match the linked native-output image")) return 2;
    puts("changed native output image rejected before execution");return 0;
}
