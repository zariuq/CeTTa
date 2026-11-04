#include "native/source_compiler_provider_v1.h"
#include <stdlib.h>
#include <string.h>

/* ABI fixture: it echoes data and supplies deliberate ownership/UTF-8 faults.
 * It grants no language binding and implements no guest semantics. */
static int call(const char *operation,const char *const *arguments,
    const size_t *lengths,size_t count,char **reply,size_t *length) {
    const char *bytes=NULL;size_t n=0;int success=1;
    if(!strcmp(operation,"echo")&&count==1u){bytes=arguments[0];n=lengths[0];}
    else if(!strcmp(operation,"failure-buffer")){bytes="released on failure";n=strlen(bytes);success=0;}
    else if(!strcmp(operation,"invalid-utf8")){bytes="\xc0\xaf";n=2;}
    else return 0;
    *reply=malloc(n+1u);if(!*reply)return 0;
    memcpy(*reply,bytes,n);(*reply)[n]=0;*length=n;return success;
}
static const CettaSourceCompilerProviderV1 provider={1u,"SourceAbiFixtureV1",call,free};
const CettaSourceCompilerProviderV1 *cetta_source_compiler_v1(void){return &provider;}
