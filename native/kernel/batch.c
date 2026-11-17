#include "batch.h"
#include <string.h>

_Static_assert(TC_EXPRESSION==5,"borrowed expression tag");
_Static_assert(sizeof(TcNode)==32 && offsetof(TcNode,kind)==0 &&
    offsetof(TcNode,ground)==1 && offsetof(TcNode,length)==4 && offsetof(TcNode,start)==8,
    "borrowed term ABI");
_Static_assert(offsetof(FlatPlan,constant)==TC_BP_CONSTANT && offsetof(FlatPlan,constants)==TC_BP_CONSTANTS &&
    offsetof(FlatPlan,guard)==TC_BP_GUARD && offsetof(FlatPlan,guards)==TC_BP_GUARDS &&
    offsetof(FlatPlan,make)==TC_BP_MAKE && offsetof(FlatPlan,makes)==TC_BP_MAKES,"flat plan ABI");

/* Admission validates constants and every carried prefix. Each newly chosen
 * row is checked before its guards run, so field projections need no repeated
 * shape or bounds checks within the stable borrowed call. */
static bool reference(const TcBatchInput *in,const uint32_t *selected,uint32_t ref,uint32_t *value) {
    uint32_t r=ref&255u,c=ref>>8;
    *value=r==in->patterns ? in->plan->constant[c] :
        in->item[(uint64_t)in->node[in->row[selected[r]]].start+c];
    return *value && *value<in->nodes;
}

/* An independent C executor of the prepared equality circuit. */
uint64_t tc_batch_c(const TcBatchInput *in,TcBatchWork *w) {
    uint32_t *cur=w->cursor,*selected=cur+TC_BATCH_SELECTED;
    uint32_t *next=cur+TC_BATCH_NEXT,*tried=cur+TC_BATCH_TRIED;
    size_t stride=(size_t)in->patterns+in->plan->makes;if (!stride) stride=1;
    size_t capacity=w->output_words/stride;
    w->produced=0;w->visited=0;
    if (!cur[0]) { cur[0]=1;cur[1]=0;next[0]=in->patterns ? in->stage[TC_BATCH_LO0] : 0;tried[0]=0; }
    for (;;) {
        if (cur[0]==2) return w->status=TC_BATCH_END;
        if (w->produced==capacity || w->visited==in->quantum) return w->status=TC_BATCH_YIELD;
        uint32_t k=cur[1];
        if (k==in->patterns) {
            if (w->used==in->budget) return w->status=TC_BATCH_EXHAUSTED;
            ++w->used;
            uint32_t *record=w->output+w->produced*stride;
            for (uint32_t i=0;i<k;++i) record[i]=selected[i];
            for (uint32_t i=0;i<in->plan->makes;++i) {
                uint32_t value;
                if (!reference(in,selected,in->plan->make[i],&value)) return w->status=TC_BATCH_INVALID;
                record[k+i]=value;
            }
            ++w->produced;
            if (!k) cur[0]=2;else cur[1]=k-1;
            continue;
        }
        const uint32_t *stage=in->stage+TC_BATCH_STAGE_WORDS*k;
        uint32_t bound=0;
        if ((in->flags&TC_BATCH_SYMMETRIC) && stage[TC_BATCH_TWIN]!=UINT32_MAX)
            bound=selected[stage[TC_BATCH_TWIN]]+(k<in->takes);
        uint32_t pos=next[k];
        if (pos<bound) pos=bound;
        if (pos<stage[TC_BATCH_LO0]) pos=stage[TC_BATCH_LO0];
        if (pos>=stage[TC_BATCH_HI0]) {
            if (pos<stage[TC_BATCH_LO1]) pos=stage[TC_BATCH_LO1];
            if (pos>=stage[TC_BATCH_HI1]) {
                if (!k) cur[0]=2;else cur[1]=k-1;
                continue;
            }
        }
        if (w->used==in->budget) return w->status=TC_BATCH_EXHAUSTED;
        ++w->used;++w->visited;next[k]=pos+1;
        bool clash=false;
        for (uint32_t j=0;j<(k<in->takes ? k : in->takes);++j) if (selected[j]==pos) clash=true;
        if (clash) continue;
        uint32_t id=in->row[pos];
        if (!id || id>=in->nodes) return w->status=TC_BATCH_INVALID;
        if (in->flags&TC_BATCH_ONE) { if (id==tried[k]) continue;tried[k]=id; }
        TcNode n=in->node[id];
        if (n.kind!=TC_EXPRESSION || n.length!=stage[TC_BATCH_LENGTH]) continue;
        if (!n.ground || n.start>in->items || n.length>in->items-n.start) return w->status=TC_BATCH_INVALID;
        selected[k]=pos;
        bool equal=true;
        for (uint32_t i=stage[TC_BATCH_GUARD_BEGIN];i<stage[TC_BATCH_GUARD_END];++i) {
            uint32_t guard=in->plan->guard[i],a=in->item[(uint64_t)n.start+((guard>>8)&255u)],b;
            if (!a || a>=in->nodes || !reference(in,selected,guard>>16,&b))
                return w->status=TC_BATCH_INVALID;
            if (a!=b) { equal=false;break; }
        }
        if (!equal) continue;
        cur[1]=++k;
        if (k<in->patterns) { next[k]=in->stage[TC_BATCH_STAGE_WORDS*k+TC_BATCH_LO0];tried[k]=0; }
    }
}

extern const unsigned char tc_batch_x86_end[],tc_batch_x86_expected[],tc_batch_x86_expected_end[];
bool cetta_kernel_batch_image_valid(void) {
    const unsigned char *code=(const unsigned char *)(uintptr_t)tc_batch_x86;
    size_t n=(size_t)((uintptr_t)tc_batch_x86_end-(uintptr_t)code);
    return n==(size_t)(tc_batch_x86_expected_end-tc_batch_x86_expected) &&
        !memcmp(code,tc_batch_x86_expected,n);
}

static bool ref_valid(const TcBatchInput *in,uint32_t ref,uint32_t depth) {
    uint32_t r=ref&255u,c=ref>>8;
    return r==in->patterns ? c<in->plan->constants :
        r<in->patterns && r<=depth && c<in->stage[TC_BATCH_STAGE_WORDS*r+TC_BATCH_LENGTH];
}

typedef struct { uintptr_t lo,hi;bool write; } Span;
static bool span(Span *s,const void *p,uint64_t count,size_t width,size_t align,bool write) {
    uintptr_t at=(uintptr_t)p;
    if (count>SIZE_MAX/width || (count && !p) || at%align || at>UINTPTR_MAX-count*width) return false;
    *s=(Span){at,at+count*width,write};return true;
}
static bool physical(const TcBatchInput *in,TcBatchWork *w) {
    Span s[9];
    if (!span(s,in,1,sizeof *in,_Alignof(TcBatchInput),false) ||
        !span(s+1,in->node,in->nodes,sizeof *in->node,_Alignof(TcNode),false) ||
        !span(s+2,in->item,in->items,4,4,false) || !span(s+3,in->row,in->rows,4,4,false) ||
        !span(s+4,in->plan,1,sizeof *in->plan,_Alignof(FlatPlan),false) ||
        !span(s+5,in->stage,in->patterns,TC_BATCH_STAGE_WORDS*4,4,false) ||
        !span(s+6,w,1,sizeof *w,_Alignof(TcBatchWork),true) ||
        !span(s+7,w->cursor,w->cursor_words,4,4,true) || !span(s+8,w->output,w->output_words,4,4,true)) return false;
    for (size_t i=0;i<9;++i) for (size_t j=i+1;j<9;++j)
        if ((s[i].write || s[j].write) && s[i].lo<s[j].hi && s[j].lo<s[i].hi &&
            s[i].hi>s[i].lo && s[j].hi>s[j].lo) return false;
    return true;
}

/* Validate the physical call boundary for both executors. */
bool tc_batch_valid(const TcBatchInput *in,TcBatchWork *w) {
    if (!in || !w || !in->node || !in->plan || !w->cursor || !w->output ||
        (in->items && !in->item) || (in->rows && !in->row) || (in->patterns && !in->stage) ||
        in->patterns>TC_BATCH_PATTERNS || in->takes>in->patterns ||
        w->cursor_words<TC_BATCH_CURSOR_WORDS || w->used>in->budget ||
        in->plan->constants>FLAT_CONSTANTS || in->plan->guards>FLAT_GUARDS || in->plan->makes>FLAT_MAKES ||
        w->cursor[0]>2 || (w->cursor[0]==1 && w->cursor[1]>in->patterns)) return false;
    uint32_t previous_guard=0;
    for (uint32_t k=0;k<in->patterns;++k) {
        const uint32_t *s=in->stage+TC_BATCH_STAGE_WORDS*k;
        if (!s[TC_BATCH_LENGTH] || s[TC_BATCH_LENGTH]>FLAT_ROW ||
            s[TC_BATCH_LO0]>s[TC_BATCH_HI0] || s[TC_BATCH_HI0]>s[TC_BATCH_LO1] ||
            s[TC_BATCH_LO1]>s[TC_BATCH_HI1] || s[TC_BATCH_HI1]>in->rows ||
            (s[TC_BATCH_TWIN]!=UINT32_MAX && s[TC_BATCH_TWIN]>=k) ||
            s[TC_BATCH_GUARD_BEGIN]!=previous_guard || s[TC_BATCH_GUARD_BEGIN]>s[TC_BATCH_GUARD_END] || s[TC_BATCH_GUARD_END]>in->plan->guards) return false;
        for (uint32_t g=s[TC_BATCH_GUARD_BEGIN];g<s[TC_BATCH_GUARD_END];++g)
            if ((in->plan->guard[g]&255u)!=k || !ref_valid(in,in->plan->guard[g]&65535u,k) || !ref_valid(in,in->plan->guard[g]>>16,k)) return false;
        previous_guard=s[TC_BATCH_GUARD_END];
    }
    if (previous_guard!=in->plan->guards || in->rows>UINT32_MAX || (in->flags&~UINT64_C(3))) return false;
    for (uint32_t m=0;m<in->plan->makes;++m)
        if (!ref_valid(in,in->plan->make[m],(uint32_t)in->patterns)) return false;
    if (!physical(in,w)) return false;
    for (uint32_t c=0;c<in->plan->constants;++c) {
        uint32_t id=in->plan->constant[c];
        if (!id || id>=in->nodes || !in->node[id].ground) return false;
    }
    if (w->cursor[0]==1) {
        for (uint32_t k=0;k<w->cursor[1];++k) {
            uint32_t at=w->cursor[TC_BATCH_SELECTED+k];
            if (at>=in->rows || w->cursor[TC_BATCH_NEXT+k]>in->rows) return false;
            uint32_t id=in->row[at];if (!id || id>=in->nodes) return false;
            TcNode n=in->node[id];
            if (!n.ground || n.kind!=TC_EXPRESSION || n.length!=in->stage[k*TC_BATCH_STAGE_WORDS+TC_BATCH_LENGTH] ||
                n.start>in->items || n.length>in->items-n.start) return false;
        }
        if (w->cursor[1]<in->patterns && w->cursor[TC_BATCH_NEXT+w->cursor[1]]>in->rows) return false;
    }
    return w->output_words>=(in->patterns+in->plan->makes ? in->patterns+in->plan->makes : 1);
}
