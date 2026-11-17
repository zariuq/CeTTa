#include "native_match.h"
#include "native_component.h"
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(NmNode) == 32 && offsetof(NmNode,a) == 8 &&
    offsetof(NmNode,b) == 16 && offsetof(NmNode,c) == 24, "node representation");
_Static_assert(sizeof(NmVariableKey) == 16 && offsetof(NmVariableKey,scope) == 8,
    "variable-key representation");
_Static_assert(sizeof(NmNodePair) == 16 && offsetof(NmNodePair,right) == 8, "pair representation");
_Static_assert((int)NM_NO_MATCH == (int)TISA_OUTCOME_REFUSED &&
    (int)NM_UNIFIED == (int)TISA_OUTCOME_FIRED &&
    (int)NM_UNSUPPORTED == (int)TISA_OUTCOME_UNSUPPORTED &&
    (int)NM_INVALID == (int)TISA_OUTCOME_INVALID_INPUT &&
    (int)NM_EXHAUSTED == (int)TISA_OUTCOME_EXHAUSTED, "outcome representation");

struct NmUnifyProgram { NmComponent component; };
extern const uint8_t nm_unify_entry[],nm_unify_image[],nm_unify_image_end[];
extern const uint8_t nm_unify_expected[],nm_unify_expected_end[];
extern const char nm_unify_text[],nm_unify_text_end[];
static const NmComponentImage image={
    "native_unify","program does not match the linked native-unifier image",
    "program does not have the native-unifier region interface",
    nm_unify_entry,nm_unify_image,nm_unify_image_end,nm_unify_expected,nm_unify_expected_end,
    nm_unify_text,nm_unify_text_end,9,{1,1,1,2,2,2,1,2,2}
};
static NmUnifyProgram *open_program(const char *path,bool native,char *error,size_t size) {
    NmUnifyProgram *p=calloc(1,sizeof *p);
    if (!p) { if (error && size) snprintf(error,size,"program allocation failed");return NULL; }
    if (!nm_component_init(&p->component,&image,path,native,error,size)) { free(p);return NULL; }
    return p;
}
NmUnifyProgram *nm_unify_program_default(bool native,char *error,size_t size) {
    return open_program(NULL,native,error,size);
}
NmUnifyProgram *nm_unify_program_open(const char *path,bool native,char *error,size_t size) {
    if (!path) { if (error && size) snprintf(error,size,"missing program text");return NULL; }
    return open_program(path,native,error,size);
}
void nm_unify_program_close(NmUnifyProgram *p) {
    if (p) { nm_component_dispose(&p->component);free(p); }
}
size_t nm_unify_state_words(const NmUnifyProgram *p) {
    return p ? ((size_t)p->component.source.prog.regions[7].min_byte_len+7u)/8u : 0;
}
const uint8_t *nm_unify_native_bytes(const NmUnifyProgram *p,size_t *size) {
    return nm_component_bytes(p ? &p->component : NULL,size);
}
static bool spans(const NmUnifyInput *in, const NmUnifyWork *work, NmComponentSpan out[NM_COMPONENT_REGIONS]) {
    return nm_component_span(out+0,in->nodes,in->nodes_count,sizeof *in->nodes,false) &&
        nm_component_span(out+1,in->children,in->children_count,sizeof *in->children,false) &&
        nm_component_span(out+2,in->initial,in->variables_count,sizeof *in->initial,false) &&
        nm_component_span(out+3,work->bindings,work->bindings_words,sizeof *work->bindings,true) &&
        nm_component_span(out+4,work->pairs,work->pairs_count,sizeof *work->pairs,true) &&
        nm_component_span(out+5,work->scan,work->scan_words,sizeof *work->scan,true) &&
        nm_component_span(out+6,in->keys,in->keys_count,sizeof *in->keys,false) &&
        nm_component_span(out+7,work->state,work->state_words,sizeof *work->state,true) &&
        nm_component_span(out+8,work->marks,work->marks_words,sizeof *work->marks,true);
}

NmCallStatus nm_unify_call(const NmUnifyProgram *p,NmExecution mode,
                           const NmUnifyInput *in,const NmUnifyWork *work,uint64_t budget,uint64_t used,
                           NmUnifyResult *result,char *error,size_t size) {
    if (!p || !in || !work || !result) {
        if (error && size) snprintf(error,size,"missing program, input, work or result");
        return NM_CALL_BAD_ARGUMENT;
    }
    NmComponentSpan regions[NM_COMPONENT_REGIONS];
    if (!spans(in,work,regions)) {
        if (error && size) snprintf(error,size,"invalid or overflowing borrowed span");
        return NM_CALL_BAD_ARGUMENT;
    }
    NmComponentResult core;
    NmCallStatus status=nm_component_call(&p->component,mode,regions,in->left,in->right,budget,used,&core,error,size);
    if (status==NM_CALL_OK || status==NM_CALL_DIFFERENCE)
        *result=(NmUnifyResult){(NmOutcome)core.outcome,core.payload,core.fuel_used};
    return status;
}
bool nm_unify_run(const NmUnifyProgram *p,NmExecution mode,
                  const NmUnifyInput *in,const NmUnifyWork *work,uint64_t budget,uint64_t used,
                  NmUnifyResult *result,char *error,size_t size) {
    return nm_unify_call(p,mode,in,work,budget,used,result,error,size)==NM_CALL_OK;
}

NmConstraintTruth nm_constraint_truth(NmCallStatus status, const NmUnifyResult *result) {
    if (status!=NM_CALL_OK || !result) return NM_TRUTH_UNAVAILABLE;
    if (result->outcome==NM_NO_MATCH) return NM_TRUTH_REFUTED;
    if (result->outcome!=NM_UNIFIED) return NM_TRUTH_UNAVAILABLE;
    return result->payload ? NM_TRUTH_OPEN : NM_TRUTH_ENTAILED;
}
