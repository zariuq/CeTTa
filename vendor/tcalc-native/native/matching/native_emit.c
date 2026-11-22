#include "native_emit.h"
#include "native_component.h"
#include "emit_layout.generated.h"
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(NmEmitNode)==40 && offsetof(NmEmitNode,source)==32,"emitted node representation");
_Static_assert(sizeof(NmEmitFrame)==32 && offsetof(NmEmitFrame,mode)==24,"emitter stack representation");
_Static_assert((int)NM_EMIT_FIRED==(int)TISA_OUTCOME_FIRED &&
    (int)NM_EMIT_UNSUPPORTED==(int)TISA_OUTCOME_UNSUPPORTED &&
    (int)NM_EMIT_INVALID==(int)TISA_OUTCOME_INVALID_INPUT &&
    (int)NM_EMIT_EXHAUSTED==(int)TISA_OUTCOME_EXHAUSTED,"emitter outcome representation");
struct NmEmitProgram { NmComponent component; };
extern const uint8_t nm_emit_entry[],nm_emit_image[],nm_emit_image_end[],nm_emit_expected[],nm_emit_expected_end[];
extern const char nm_emit_text[],nm_emit_text_end[];
static const NmComponentImage image={
    "native_emit","program does not match the linked native-output image",
    "program does not have the native-output region interface",
    nm_emit_entry,nm_emit_image,nm_emit_image_end,nm_emit_expected,nm_emit_expected_end,
    nm_emit_text,nm_emit_text_end,9,{1,1,1,2,2,2,2,2,1}
};
static NmEmitProgram *open_program(const char *path,bool native,char *error,size_t size) {
    NmEmitProgram *p=calloc(1,sizeof *p);
    if (!p) { if (error && size) snprintf(error,size,"program allocation failed");return NULL; }
    if (!nm_component_init(&p->component,&image,path,native,error,size)) { free(p);return NULL; }
    return p;
}
NmEmitProgram *nm_emit_program_default(bool native,char *error,size_t size) { return open_program(NULL,native,error,size); }
NmEmitProgram *nm_emit_program_open(const char *path,bool native,char *error,size_t size) {
    if (!path) { if (error && size) snprintf(error,size,"missing program text");return NULL; }
    return open_program(path,native,error,size);
}
void nm_emit_program_close(NmEmitProgram *p) { if (p) { nm_component_dispose(&p->component);free(p); } }
size_t nm_emit_state_words(const NmEmitProgram *p) {
    return p ? ((size_t)p->component.source.prog.regions[7].min_byte_len+7u)/8u : 0;
}
const uint8_t *nm_emit_native_bytes(const NmEmitProgram *p,size_t *size) {
    return nm_component_bytes(p ? &p->component : NULL,size);
}
static bool spans(const NmEmitInput *in,const NmEmitWork *w,NmComponentSpan out[NM_COMPONENT_REGIONS]) {
    return nm_component_span(out+0,in->nodes,in->nodes_count,sizeof *in->nodes,false) &&
        nm_component_span(out+1,in->children,in->children_count,sizeof *in->children,false) &&
        nm_component_span(out+2,in->bindings,in->variables_count,sizeof *in->bindings,false) &&
        nm_component_span(out+3,w->nodes,w->nodes_count,sizeof *w->nodes,true) &&
        nm_component_span(out+4,w->children,w->children_count,sizeof *w->children,true) &&
        nm_component_span(out+5,w->memo,w->memo_words,sizeof *w->memo,true) &&
        nm_component_span(out+6,w->stack,w->stack_count,sizeof *w->stack,true) &&
        nm_component_span(out+7,w->state,w->state_words,sizeof *w->state,true) &&
        nm_component_span(out+8,in->keys,in->keys_count,sizeof *in->keys,false);
}
NmCallStatus nm_emit_call(const NmEmitProgram *p,NmExecution mode,const NmEmitInput *in,const NmEmitWork *work,
                          uint64_t budget,uint64_t used,NmEmitResult *result,char *error,size_t size) {
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
    NmCallStatus status=nm_component_call(&p->component,mode,regions,in->root,work->nodes_count,budget,used,&core,error,size);
    if (status!=NM_CALL_OK) return status;
    *result=(NmEmitResult){.outcome=(NmEmitOutcome)core.outcome,.fuel_used=core.fuel_used};
    if (core.outcome==TISA_OUTCOME_FIRED) result->root=core.payload;else result->reason=core.payload;
    if (core.outcome!=TISA_OUTCOME_INVALID_INPUT && work->state && work->state_words>=nm_emit_state_words(p)) {
        result->nodes_written=work->state[NE_STATE_OUTPUT_COUNT];
        result->children_written=work->state[NE_STATE_EDGE_COUNT];
        result->peak_stack=work->state[NE_STATE_PEAK];result->expanded=work->state[NE_STATE_EXPANDED];
        if (result->nodes_written>work->nodes_count || result->children_written>work->children_count ||
            result->peak_stack>work->stack_count || (result->root && result->root>=result->nodes_written)) {
            if (error && size) snprintf(error,size,"emitted counts exceed the declared workspace");
            return NM_CALL_DIFFERENCE;
        }
    }
    return NM_CALL_OK;
}
