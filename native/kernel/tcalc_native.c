/* tcalc's x86 kernel: its unifier (tcunify.metta) and its term builder
 * (tcbuild.metta), each a p1 program over tcalc's own term store, called with
 * the store's tables as its regions (tcalc_bridge.h). */
#include "tcalc_native.h"
#include "tcalc_bridge.h"
#include "../../vendor/tcalc-native/native/matching/native_component.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define IMAGE(stem) \
    extern const uint8_t ck_##stem##_entry[],ck_##stem##_image[],ck_##stem##_image_end[], \
        ck_##stem##_expected[],ck_##stem##_expected_end[]; \
    extern const char ck_##stem##_text[],ck_##stem##_text_end[];
IMAGE(tcunify)
IMAGE(tcbuild)
#undef IMAGE
static const NmComponentImage unify_image={"native_tcunify","native tcalc unifier image differs from its expected bytes",
    "native tcalc unifier has the wrong region profile",ck_tcunify_entry,ck_tcunify_image,ck_tcunify_image_end,
    ck_tcunify_expected,ck_tcunify_expected_end,ck_tcunify_text,ck_tcunify_text_end,8,{1,1,2,2,2,2,2,2}};
static const NmComponentImage build_image={"native_tcbuild","native tcalc term builder image differs from its expected bytes",
    "native tcalc term builder has the wrong region profile",ck_tcbuild_entry,ck_tcbuild_image,ck_tcbuild_image_end,
    ck_tcbuild_expected,ck_tcbuild_expected_end,ck_tcbuild_text,ck_tcbuild_text_end,9,{1,1,1,1,2,2,2,2,2}};
static NmComponent unifier,builder;
static _Atomic bool loaded;
static bool ready;
static char failure[256];
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void init(void) {
    ready=nm_component_init(&unifier,&unify_image,NULL,true,failure,sizeof failure) &&
          nm_component_init(&builder,&build_image,NULL,true,failure,sizeof failure);
    atomic_store_explicit(&loaded,true,memory_order_release);
}
static bool load(void) {
    if (!atomic_load_explicit(&loaded,memory_order_acquire)) pthread_once(&once,init);
    return ready;
}

bool cetta_kernel_tcalc_native_ready(char *error,size_t size) {
    if (!load() && error && size) snprintf(error,size,"%s",failure);
    return ready;
}

/* A region's two frame words. */
#define REGION(pointer,count) (uint64_t)(uintptr_t)(pointer),(uint64_t)(count)*sizeof *(pointer)

static bool call(const NmComponent *c,const uint64_t *words,uint64_t limit,uint64_t *answer) {
    NmComponentResult r;
    if (nm_component_call_words(c,words,UINT64_MAX,0,&r)!=NM_CALL_OK || r.outcome!=TISA_OUTCOME_FIRED ||
        r.payload>limit) return false;
    *answer=r.payload;return true;
}

uint64_t cetta_kernel_tcalc_unify(TcUnifyStore *s) {
    uint64_t answer;
    if (!load() || !s || !s->state || s->state_words<TU_FACTS) return TU_R_FAULT;
    const uint64_t words[]={REGION(s->node,s->nodes),REGION(s->item,s->items),REGION(s->local,s->locals),
        REGION(s->unknown,s->unknowns),REGION(s->trail,s->trail_cap),REGION(s->work,s->work_cap),
        REGION(s->seen,s->seen_cap),REGION(s->state,s->state_words)};
    return call(&unifier,words,TU_R_FAULT,&answer) ? answer : TU_R_FAULT;
}

uint64_t cetta_kernel_tcalc_build(TcBuildStore *s) {
    uint64_t answer;
    if (!load() || !s || !s->state || s->state_words<TB_FACTS) return TB_R_FAULT;
    const uint64_t words[]={REGION(s->node,s->nodes),REGION(s->item,s->items),REGION(s->local,s->locals),
        REGION(s->unknown,s->unknowns),REGION(s->seen,s->seen_cap),REGION(s->memo,s->memo_cap),
        REGION(s->out,s->out_cap),REGION(s->stack,s->stack_cap),REGION(s->state,s->state_words)};
    return call(&builder,words,TB_R_FAULT,&answer) ? answer : TB_R_FAULT;
}
