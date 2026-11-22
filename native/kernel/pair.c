#include "pair.h"
#include "../../vendor/tcalc-native/native/matching/native_component.h"
#include <stdlib.h>

struct CettaKernelPairProgram { NmComponent component,word; };
extern const uint8_t ck_pair_entry[],ck_pair_image[],ck_pair_image_end[],ck_pair_expected[],ck_pair_expected_end[];
extern const char ck_pair_text[],ck_pair_text_end[];
static const NmComponentImage image={"native_pair","native matching policy image differs from its expected bytes",
    "native matching policy has the wrong region profile",ck_pair_entry,ck_pair_image,ck_pair_image_end,
    ck_pair_expected,ck_pair_expected_end,ck_pair_text,ck_pair_text_end,2,{1,2}};
extern const uint8_t ck_word_entry[],ck_word_image[],ck_word_image_end[],ck_word_expected[],ck_word_expected_end[];
extern const char ck_word_text[],ck_word_text_end[];
static const NmComponentImage word_image={"native_word","native scalar image differs from its expected bytes",
    "native scalar has the wrong region profile",ck_word_entry,ck_word_image,ck_word_image_end,
    ck_word_expected,ck_word_expected_end,ck_word_text,ck_word_text_end,0,{0}};
CettaKernelPairProgram *cetta_kernel_pair_program(char *error,size_t size) {
    CettaKernelPairProgram *p=calloc(1,sizeof *p);if (!p) return NULL;
    if (!nm_component_init(&p->component,&image,NULL,true,error,size)) { free(p);return NULL; }
    if (!nm_component_init(&p->word,&word_image,NULL,true,error,size)) { nm_component_dispose(&p->component);free(p);return NULL; }
    return p;
}
void cetta_kernel_pair_program_free(CettaKernelPairProgram *p) {
    if (p) { nm_component_dispose(&p->component);nm_component_dispose(&p->word);free(p); }
}
size_t cetta_kernel_pair_bytes(const CettaKernelPairProgram *p) {
    size_t size=0;if (p) (void)nm_component_bytes(&p->component,&size);return size;
}
bool cetta_kernel_pair_call(const CettaKernelPairProgram *p,const CettaKernelPairFacts *facts,
    uint64_t budget,uint64_t used,CettaKernelPairPlan *plan) {
    if (!p || !facts || !plan) return false;
    uint64_t result[4]={0};NmComponentSpan spans[NM_COMPONENT_REGIONS];   /* a call reads the declared two */
    if (!nm_component_span(spans,facts,1,sizeof *facts,false) || !nm_component_span(spans+1,result,4,sizeof *result,true)) return false;
    NmComponentResult r;char error[128];
    if (nm_component_call(&p->component,NM_NATIVE,spans,0,0,budget,used,&r,error,sizeof error)!=NM_CALL_OK ||
        r.outcome!=TISA_OUTCOME_FIRED || r.payload>NP_A_LIST) return false;
    *plan=(CettaKernelPairPlan){r.payload,result[0],result[1],result[2],result[3],r.fuel_used};return true;
}

bool cetta_kernel_pair_word_call(const CettaKernelPairProgram *p,uint64_t left,uint64_t right,
        uint64_t budget,uint64_t used,bool *equal,uint64_t *fuel) {
    if (!p || !equal || !fuel) return false;
    NmComponentResult r;
    if (nm_component_call_scalar(&p->word,left,right,budget,used,&r)!=NM_CALL_OK ||
        r.outcome!=TISA_OUTCOME_FIRED || r.payload>1) return false;
    *equal=r.payload!=0;*fuel=r.fuel_used;return true;
}
