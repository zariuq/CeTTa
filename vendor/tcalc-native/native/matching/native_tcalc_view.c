#include "native_tcalc_view.h"

static bool live(const NmTcalcTermView *source, NmHostValue value) {
    return source && source->terms && value.syntax && value.syntax<=source->last_term;
}
static NmViewStatus inspect(void *context, NmHostValue value, NmViewNode *node) {
    const NmTcalcTermView *source=context;
    if (!live(source,value)) return NM_VIEW_FAULT;
    TcTerm term=(TcTerm)value.syntax;
    switch (tc_kind(source->terms,term)) {
    case TC_SYMBOL:case TC_INTEGER:case TC_STRING:
        *node=(NmViewNode){.kind=NM_NODE_CONSTANT,.constant=term};break;
    case TC_LOCAL:
        *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={term,value.context}};break;
    case TC_UNKNOWN:
        *node=(NmViewNode){.kind=NM_NODE_VARIABLE,.variable={term,source->state_scope}};break;
    case TC_EXPRESSION:
        *node=(NmViewNode){.kind=NM_NODE_TUPLE,.children=tc_length(source->terms,term)};break;
    default:return NM_VIEW_DEFER;
    }
    return NM_VIEW_READY;
}
static NmViewStatus child(void *context, NmHostValue value, uint64_t index, NmHostValue *out) {
    const NmTcalcTermView *source=context;
    if (!live(source,value) || tc_kind(source->terms,(TcTerm)value.syntax)!=TC_EXPRESSION ||
        index>=tc_length(source->terms,(TcTerm)value.syntax)) return NM_VIEW_FAULT;
    *out=(NmHostValue){tc_item(source->terms,(TcTerm)value.syntax,(uint32_t)index),value.context};
    return live(source,*out) ? NM_VIEW_READY : NM_VIEW_FAULT;
}
NmTermView nm_tcalc_term_view(NmTcalcTermView *source) {
    return (NmTermView){source,inspect,child,NULL};
}
