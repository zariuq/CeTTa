#include "native_query.h"
#include <stdio.h>
#include <string.h>

static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    (void)context;(void)value;*node=(NmViewNode){.kind=NM_NODE_CONSTANT};return NM_VIEW_READY;
}
int main(void) {
    char error[256];NmUnifyProgram *program=nm_unify_program_default(true,error,sizeof error);
    if (!program) { fprintf(stderr,"unifier setup failed: %s\n",error);return 2; }
    NmQueryView view={.terms={NULL,inspect,NULL,NULL}};
    NmQueryLimits limits={.import={3,0,0,0},.pairs=1,.scan=1,.memo=1};NmQuery *q=NULL;
    NmQueryStatus status=nm_query_open(program,NM_NATIVE,view,NULL,0,0,NULL,0,limits,1000,0,
        &q,error,sizeof error);
    nm_query_free(q);nm_unify_program_close(program);
    if (status!=NM_QUERY_FAULT || !strstr(error,"selection code differs")) return 1;
    puts("changed native selection image rejected before execution");return 0;
}
