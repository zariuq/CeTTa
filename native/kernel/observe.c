#include "observe.h"
#include "../../vendor/tcalc-native/native/matching/native_component.h"
#include <stdlib.h>
struct CettaKernelObserveProgram { NmComponent component; };
extern const uint8_t ck_observe_entry[],ck_observe_image[],ck_observe_image_end[],ck_observe_expected[],ck_observe_expected_end[];
extern const char ck_observe_text[],ck_observe_text_end[];
static const NmComponentImage image={"native_observe","native observation image differs from its expected bytes",
    "native observation has the wrong region profile",ck_observe_entry,ck_observe_image,ck_observe_image_end,
    ck_observe_expected,ck_observe_expected_end,ck_observe_text,ck_observe_text_end,1,{1}};
CettaKernelObserveProgram *cetta_kernel_observe_program(char *error,size_t size) {
    CettaKernelObserveProgram *p=calloc(1,sizeof *p);if (!p) return NULL;
    if (!nm_component_init(&p->component,&image,NULL,true,error,size)) { free(p);return NULL; }
    return p;
}
void cetta_kernel_observe_program_free(CettaKernelObserveProgram *p) {
    if (p) { nm_component_dispose(&p->component);free(p); }
}
bool cetta_kernel_observe_call(const CettaKernelObserveProgram *p,const CettaKernelObserveFacts *facts,
    uint64_t budget,uint64_t used,uint64_t *action,uint64_t *fuel) {
    if (!p || !facts || !action || !fuel) return false;
    NmComponentSpan spans[NM_COMPONENT_REGIONS]={0};
    if (!nm_component_span(spans,facts,1,sizeof *facts,false)) return false;
    NmComponentResult r;char error[128];
    if (nm_component_call(&p->component,NM_NATIVE,spans,0,0,budget,used,&r,error,sizeof error)!=NM_CALL_OK ||
        r.outcome!=TISA_OUTCOME_FIRED || r.payload>NO_A_FINISH) return false;
    *action=r.payload;*fuel=r.fuel_used;return true;
}
