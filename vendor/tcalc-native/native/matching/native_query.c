#include "native_query.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NmQuery {
    const NmUnifyProgram *program;
    NmExecution execution;
    NmQueryView view;
    NmQueryLimits limits;
    NmHostValue *patterns;
    NmQueryRow *rows;
    size_t count, takes, row_count;
    size_t *positions;
    size_t *comparison_positions;
    uint64_t *selected;
    NmHostConstraint *constraints;
    NmPreparedMatch *prepared;
    NmUnifyWork work;
    NmQueryStats stats;
    uint64_t fuel_budget, fuel_used;
    NmQueryStatus status;
    bool advance, done;
    NmQueryTrace trace;
    void *trace_context;
};

extern uint64_t nm_selection_advance(size_t *,size_t,size_t);
extern uint64_t nm_selection_allowed(const size_t *,size_t,size_t);
extern const uint8_t nm_selection_advance_end[],nm_selection_advance_expected[],nm_selection_advance_expected_end[];
extern const uint8_t nm_selection_allowed_end[],nm_selection_allowed_expected[],nm_selection_allowed_expected_end[];
_Static_assert(sizeof(size_t)==8,"native selection requires 64-bit size_t");
static bool selection_image_valid(void) {
    const uint8_t landing[]={0xf3,0x0f,0x1e,0xfa};
    const uint8_t *advance_code=(const uint8_t *)(uintptr_t)nm_selection_advance;
    const uint8_t *allowed_code=(const uint8_t *)(uintptr_t)nm_selection_allowed;
    size_t advance_bytes=(size_t)((uintptr_t)nm_selection_advance_end-(uintptr_t)advance_code);
    size_t allowed_bytes=(size_t)((uintptr_t)nm_selection_allowed_end-(uintptr_t)allowed_code);
    return advance_bytes>=sizeof landing && allowed_bytes>=sizeof landing &&
        !memcmp(advance_code,landing,sizeof landing) && !memcmp(allowed_code,landing,sizeof landing) &&
        advance_bytes==(size_t)((uintptr_t)nm_selection_advance_expected_end-(uintptr_t)nm_selection_advance_expected) &&
        allowed_bytes==(size_t)((uintptr_t)nm_selection_allowed_expected_end-(uintptr_t)nm_selection_allowed_expected) &&
        !memcmp(advance_code,nm_selection_advance_expected,advance_bytes) &&
        !memcmp(allowed_code,nm_selection_allowed_expected,allowed_bytes);
}

static NmQueryStatus fail(char *error, size_t size, NmQueryStatus status, const char *format, ...) {
    if (error && size) {
        va_list args;va_start(args,format);vsnprintf(error,size,format,args);va_end(args);
    }
    return status;
}
static NmQueryStatus terminal(NmQuery *q, NmQueryStatus status) {
    q->done=true;q->status=status;return status;
}
static void work_free(NmQuery *q) {
    free(q->work.bindings);free(q->work.pairs);free(q->work.scan);
    free(q->work.state);free(q->work.marks);memset(&q->work,0,sizeof q->work);
    nm_match_prepared_free(q->prepared);q->prepared=NULL;
}
void nm_query_free(NmQuery *q) {
    if (!q) return;
    work_free(q);free(q->patterns);free(q->rows);free(q->positions);free(q->comparison_positions);
    free(q->selected);free(q->constraints);free(q);
}
static int occurrence_compare(const void *a, const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;
    return x<y ? -1 : x>y;
}
NmQueryStatus nm_query_open(const NmUnifyProgram *program, NmExecution execution,
                            NmQueryView view, const NmHostValue *patterns, size_t count,
                            size_t takes, const NmQueryRow *rows, size_t row_count,
                            NmQueryLimits limits, uint64_t fuel_budget, uint64_t fuel_used,
                            NmQuery **out, char *error, size_t error_size) {
    if (out) *out=NULL;
    if (error && error_size) error[0]=0;
    if (!out || !program || !view.terms.inspect || takes>count ||
        (count && !patterns) || (row_count && !rows) ||
        (execution!=NM_REFERENCE && execution!=NM_NATIVE && execution!=NM_COMPARE))
        return fail(error,error_size,NM_QUERY_FAULT,"invalid query arguments");
    if (execution!=NM_REFERENCE && !nm_unify_native_bytes(program,NULL))
        return fail(error,error_size,NM_QUERY_FAULT,"query requires a native program");
    if (execution!=NM_REFERENCE && !selection_image_valid())
        return fail(error,error_size,NM_QUERY_FAULT,"query selection code differs from its registration image");
    if (fuel_used>fuel_budget)
        return fail(error,error_size,NM_QUERY_FAULT,"invalid query fuel receipt");
    if (view.valid && !view.valid(view.pin)) return NM_QUERY_INVALIDATED;
    if (!limits.memo || (limits.memo&(limits.memo-1)))
        return fail(error,error_size,NM_QUERY_FAULT,"query memo capacity must be a positive power of two");
    if (count>SIZE_MAX/sizeof *patterns || count>SIZE_MAX/sizeof(size_t) ||
        count>SIZE_MAX/sizeof(uint64_t) || count>SIZE_MAX/sizeof(NmHostConstraint) ||
        row_count>SIZE_MAX/sizeof *rows || row_count>SIZE_MAX/sizeof(uint64_t))
        return fail(error,error_size,NM_QUERY_RESOURCE,"query vector size overflow");
    NmQuery *q=calloc(1,sizeof *q);
    if (!q) return fail(error,error_size,NM_QUERY_RESOURCE,"query allocation failed");
    q->program=program;q->execution=execution;q->view=view;q->limits=limits;
    q->count=count;q->takes=takes;q->row_count=row_count;
    q->fuel_budget=fuel_budget;q->fuel_used=fuel_used;q->stats.fuel_used=fuel_used;
    q->status=NM_QUERY_YIELD;
    if (count) {
        q->patterns=malloc(count*sizeof *q->patterns);
        q->positions=calloc(count,sizeof *q->positions);
        if (execution==NM_COMPARE) q->comparison_positions=malloc(count*sizeof *q->comparison_positions);
        q->selected=malloc(count*sizeof *q->selected);
        q->constraints=malloc(count*sizeof *q->constraints);
        if (!q->patterns || !q->positions || !q->selected || !q->constraints ||
            (execution==NM_COMPARE && !q->comparison_positions)) {
            nm_query_free(q);return fail(error,error_size,NM_QUERY_RESOURCE,"query pattern allocation failed");
        }
        memcpy(q->patterns,patterns,count*sizeof *patterns);
    }
    if (row_count) {
        q->rows=malloc(row_count*sizeof *q->rows);
        uint64_t *ids=malloc(row_count*sizeof *ids);
        if (!q->rows || !ids) {
            free(ids);nm_query_free(q);return fail(error,error_size,NM_QUERY_RESOURCE,"query row allocation failed");
        }
        memcpy(q->rows,rows,row_count*sizeof *rows);
        for (size_t i=0;i<row_count;++i) ids[i]=rows[i].occurrence;
        if (row_count>1) qsort(ids,row_count,sizeof *ids,occurrence_compare);
        bool duplicate=false;
        for (size_t i=1;i<row_count;++i) if (ids[i]==ids[i-1]) { duplicate=true;break; }
        free(ids);
        if (duplicate) {
            nm_query_free(q);return fail(error,error_size,NM_QUERY_FAULT,"duplicate row occurrence id");
        }
    }
    *out=q;return NM_QUERY_YIELD;
}

static bool work_create(NmQuery *q) {
    const NmUnifyInput *in=nm_match_input(q->prepared);
    NmUnifyWork *w=&q->work;
    if (!q->limits.pairs || !q->limits.scan || q->limits.memo>UINT32_MAX ||
        in->nodes_count>SIZE_MAX-in->variables_count ||
        q->limits.memo>SIZE_MAX-in->nodes_count-in->variables_count) return false;
    w->bindings_words=in->variables_count;w->pairs_count=q->limits.pairs;
    w->scan_words=q->limits.scan;w->state_words=nm_unify_state_words(q->program);
    w->marks_words=in->nodes_count+in->variables_count+q->limits.memo;
    if (w->bindings_words>SIZE_MAX/sizeof *w->bindings ||
        w->pairs_count>SIZE_MAX/sizeof *w->pairs || w->scan_words>SIZE_MAX/sizeof *w->scan ||
        w->state_words>SIZE_MAX/sizeof *w->state || w->marks_words>SIZE_MAX/sizeof *w->marks) return false;
    w->bindings=calloc(w->bindings_words ? w->bindings_words : 1,sizeof *w->bindings);
    w->pairs=calloc(w->pairs_count ? w->pairs_count : 1,sizeof *w->pairs);
    w->scan=calloc(w->scan_words ? w->scan_words : 1,sizeof *w->scan);
    w->state=calloc(w->state_words ? w->state_words : 1,sizeof *w->state);
    w->marks=calloc(w->marks_words,sizeof *w->marks);
    return w->bindings && w->pairs && w->scan && w->state && w->marks;
}
static uint64_t advance_reference(size_t *positions, size_t count, size_t rows) {
    if (!count) return 0;
    if (!rows) return 2;
    for (size_t i=0;i<count;++i) if (positions[i]>=rows) return 2;
    for (size_t i=count;i-->0;) {
        if (positions[i]<rows-1) { ++positions[i];return 1; }
        positions[i]=0;
    }
    return 0;
}
static uint64_t advance(NmQuery *q) {
    if (q->execution==NM_REFERENCE) return advance_reference(q->positions,q->count,q->row_count);
    if (q->execution==NM_COMPARE && q->count)
        memcpy(q->comparison_positions,q->positions,q->count*sizeof *q->positions);
    uint64_t result=nm_selection_advance(q->positions,q->count,q->row_count);
    if (q->execution==NM_COMPARE && (result!=advance_reference(q->comparison_positions,q->count,q->row_count) ||
        (q->count && memcmp(q->positions,q->comparison_positions,q->count*sizeof *q->positions)))) return 2;
    return result;
}
static uint64_t allowed_reference(const size_t *positions, size_t count, size_t takes) {
    if (takes>count) return 2;
    bool allowed=true;
    for (size_t i=0;i<count;++i)
        for (size_t j=0;j<i && j<takes;++j) if (positions[i]==positions[j]) allowed=false;
    return allowed;
}
static uint64_t selection(NmQuery *q) {
    for (size_t i=0;i<q->count;++i) {
        size_t row=q->positions[i];
        q->selected[i]=q->rows[row].occurrence;
        q->constraints[i]=(NmHostConstraint){q->patterns[i],q->rows[row].value};
    }
    if (q->execution==NM_REFERENCE) return allowed_reference(q->positions,q->count,q->takes);
    uint64_t allowed=nm_selection_allowed(q->positions,q->count,q->takes);
    if (q->execution==NM_COMPARE && allowed!=allowed_reference(q->positions,q->count,q->takes)) return 2;
    return allowed;
}
static void trace(NmQuery *q, NmQueryEventKind kind, bool allowed, NmViewStatus imported,
                   NmUnifyResult result) {
    if (q->trace) {
        NmQueryEvent event={kind,q->stats.candidates,q->selected,q->count,allowed,imported,result,
            kind==NM_QUERY_CORE_RESULT ? nm_constraint_truth(NM_CALL_OK,&result) : NM_TRUTH_UNAVAILABLE};
        q->trace(q->trace_context,&event);
    }
}
static bool add_stat(uint64_t *sum, size_t value) {
    if (value>UINT64_MAX-*sum) return false;
    *sum+=(uint64_t)value;return true;
}
NmQueryStatus nm_query_next(NmQuery *q, uint64_t quantum, NmQueryAnswer *answer,
                            char *error, size_t error_size) {
    if (answer) memset(answer,0,sizeof *answer);
    if (error && error_size) error[0]=0;
    if (!q || !answer) return fail(error,error_size,NM_QUERY_FAULT,"missing query or answer");
    if (q->done) return q->status;
    work_free(q);
    if (q->view.valid && !q->view.valid(q->view.pin)) return terminal(q,NM_QUERY_INVALIDATED);
    if (q->count && !q->row_count) return terminal(q,NM_QUERY_END);
    for (uint64_t proposals=0;proposals<quantum;++proposals) {
        if (q->advance) {
            uint64_t moved=advance(q);
            if (moved!=1) return terminal(q,moved ? NM_QUERY_FAULT : NM_QUERY_END);
        }
        q->advance=true;
        if (q->stats.candidates==UINT64_MAX) return terminal(q,NM_QUERY_RESOURCE);
        ++q->stats.candidates;
        uint64_t allowed=selection(q);
        if (allowed>1) return terminal(q,NM_QUERY_FAULT);
        trace(q,NM_QUERY_PROPOSED,allowed!=0,NM_VIEW_READY,(NmUnifyResult){0});
        if (q->view.valid && !q->view.valid(q->view.pin)) return terminal(q,NM_QUERY_INVALIDATED);
        if (!allowed) continue;
        NmImportStats imported;
        NmViewStatus status=nm_match_prepare_constraints(&q->view.terms,q->constraints,q->count,
            q->limits.import,&q->prepared,&imported);
        if (!add_stat(&q->stats.inspections,imported.inspections) ||
            !add_stat(&q->stats.children,imported.children) || !add_stat(&q->stats.bindings,imported.bindings))
            return terminal(q,NM_QUERY_RESOURCE);
        trace(q,NM_QUERY_IMPORTED,true,status,(NmUnifyResult){0});
        if (q->view.valid && !q->view.valid(q->view.pin)) return terminal(q,NM_QUERY_INVALIDATED);
        if (status!=NM_VIEW_READY) return terminal(q,status==NM_VIEW_DEFER ? NM_QUERY_DEFER :
            status==NM_VIEW_RESOURCE ? NM_QUERY_RESOURCE :
            status==NM_VIEW_INTERRUPTED ? NM_QUERY_INTERRUPTED : NM_QUERY_FAULT);
        if (!work_create(q)) return terminal(q,NM_QUERY_RESOURCE);
        NmUnifyResult result;
        if (q->stats.unifier_calls==UINT64_MAX) return terminal(q,NM_QUERY_RESOURCE);
        ++q->stats.unifier_calls;
        NmCallStatus called=nm_unify_call(q->program,q->execution,nm_match_input(q->prepared),&q->work,
            q->fuel_budget,q->fuel_used,&result,error,error_size);
        if (called!=NM_CALL_OK)
            return terminal(q,called==NM_CALL_RESOURCE ? NM_QUERY_RESOURCE : NM_QUERY_FAULT);
        q->fuel_used=result.fuel_used;q->stats.fuel_used=q->fuel_used;
        trace(q,NM_QUERY_CORE_RESULT,true,NM_VIEW_READY,result);
        if (q->view.valid && !q->view.valid(q->view.pin)) return terminal(q,NM_QUERY_INVALIDATED);
        if (result.outcome==NM_UNIFIED) {
            *answer=(NmQueryAnswer){q->selected,q->count,q->takes,q->prepared,&q->work,result};
            return NM_QUERY_ANSWER;
        }
        if (result.outcome==NM_EXHAUSTED) return terminal(q,NM_QUERY_EXHAUSTED);
        if (result.outcome==NM_UNSUPPORTED)
            return terminal(q,result.payload==6 || result.payload==7 || result.payload==8 ||
                result.payload==9 || result.payload==10 ? NM_QUERY_RESOURCE : NM_QUERY_FAULT);
        if (result.outcome!=NM_NO_MATCH) return terminal(q,NM_QUERY_FAULT);
        work_free(q);
    }
    return NM_QUERY_YIELD;
}
NmQueryStats nm_query_stats(const NmQuery *q) { return q ? q->stats : (NmQueryStats){0}; }
void nm_query_set_trace(NmQuery *q, NmQueryTrace fn, void *context) {
    if (q) { q->trace=fn;q->trace_context=context; }
}
