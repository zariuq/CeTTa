#include "native_component.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TISA_DEV_REFERENCE
enum { GUARD=64,GUARD_BYTE=0xD3 };
typedef struct { uint8_t *owner;NmComponentSpan span; } Copy;
#endif
static bool failed(char *error,size_t size,const char *format,...) {
    if (error && size) {
        va_list args;va_start(args,format);vsnprintf(error,size,format,args);va_end(args);
    }
    return false;
}
static NmCallStatus call_failed(NmCallStatus status,char *error,size_t size,const char *message) {
    failed(error,size,"%s",message);return status;
}
static bool linked_valid(const NmComponentImage *i) {
    const uint8_t landing[]={0xf3,0x0f,0x1e,0xfa};
    size_t body=(size_t)((uintptr_t)i->body_end-(uintptr_t)i->body);
    return (uintptr_t)i->body-(uintptr_t)i->entry==sizeof landing &&
        body==(size_t)((uintptr_t)i->expected_end-(uintptr_t)i->expected) &&
        !memcmp(i->entry,landing,sizeof landing) && !memcmp(i->body,i->expected,body);
}
void nm_component_dispose(NmComponent *c) {
    if (!c) return;
    tisa_x86_free(&c->native);p1_free(&c->source);
}
bool nm_component_init(NmComponent *c,const NmComponentImage *i,const char *path,
                        bool native,char *error,size_t size) {
    memset(c,0,sizeof *c);c->image=i;
    if (!i || i->regions>NM_COMPONENT_REGIONS)
        return failed(error,size,"invalid component descriptor");
    if (native && !linked_valid(i)) return failed(error,size,"%s",i->image_error);
    P1Error parse;bool loaded;
    if (path) {
        TcIsaTextStyle style;loaded=p1_load_file(path,&c->source,&style,&parse);
    } else {
        size_t length=(size_t)((uintptr_t)i->text_end-(uintptr_t)i->text);
        if (length && i->text[length-1]=='\n') --length;
        loaded=p1_parse(i->text,length,TISA_TEXT_HOL,&c->source,&parse);
    }
    if (!loaded) return failed(error,size,"program text %u:%u: %s",parse.line,parse.column,parse.message);
    c->source.prog.name=i->name;
    bool profile=c->source.prog.nregions==i->regions && c->source.prog.init_reg_mask==0;
    for (unsigned r=0;profile && r<i->regions;++r) profile=c->source.prog.regions[r].mode==i->modes[r];
    if (!profile || !tisa_admit(&c->source.prog,NULL,error,size)) {
        if (!profile) failed(error,size,"%s",i->profile_error);
        nm_component_dispose(c);return false;
    }
    if (native && path) {
        bool emitted=tisa_x86_emit(&c->source.prog,&c->native,error,size);
        size_t length=(size_t)((uintptr_t)i->body_end-(uintptr_t)i->body);
        if (!emitted || c->native.len!=length || memcmp(c->native.bytes,i->body,length)) {
            if (emitted) failed(error,size,"%s",i->image_error);
            nm_component_dispose(c);return false;
        }
    }
    c->has_native=native;if (error && size) error[0]=0;return true;
}
const uint8_t *nm_component_bytes(const NmComponent *c,size_t *size) {
    if (size) *size=c && c->has_native ? (size_t)((uintptr_t)c->image->body_end-(uintptr_t)c->image->entry) : 0;
    return c && c->has_native ? c->image->entry : NULL;
}
static void frame_make(TcIsaRuntimeFrame *frame,const NmComponentSpan spans[NM_COMPONENT_REGIONS],
                        unsigned n,uint64_t arg0,uint64_t arg1,uint64_t budget,uint64_t used) {
    uint64_t words[2*NM_COMPONENT_REGIONS]={0};
    for (unsigned r=0;r<n;++r) { words[2*r]=(uint64_t)(uintptr_t)spans[r].base;words[2*r+1]=spans[r].bytes; }
    memset(frame,0,sizeof *frame);memcpy(frame,words,sizeof words);
    frame->arg0=arg0;frame->arg1=arg1;frame->fuel_budget=budget;frame->fuel_used=used;
}
/* A native call runs in one frame per thread, at a fixed address: a call fills it, runs the
   image and reads the result, and images make no calls, so no other call runs in it meanwhile.
   tisa_entry_valid depends on the frame's address, the declared regions' words, the receipt
   and the initial registers; component profiles have no initial registers (nm_component_init).
   So once a component's region words have been validated in this frame, a later call with the
   same words needs only the receipt check. */
typedef struct { const NmComponentImage *image;uint64_t words[2*NM_COMPONENT_REGIONS]; } Validated;
enum { VALIDATED_SLOTS=8 };
static _Thread_local TcIsaRuntimeFrame native_frame;
static _Thread_local Validated validated[VALIDATED_SLOTS];
/* Only the declared regions' words are written: neither the image nor tisa_entry_valid reads
   the others. The result is read word by word: the image stores status and ret_val separately,
   and one wider load across both stores would wait for them to reach the cache. */
static void native_run(const NmComponent *c,const uint64_t *in,uint64_t arg0,uint64_t arg1,
                       uint64_t budget,uint64_t used,NmComponentResult *out) {
    TcIsaRuntimeFrame *f=&native_frame;uint64_t *words=(uint64_t *)f;unsigned n=c->image->regions;
    Validated *v=&validated[((uintptr_t)c->image>>4)%VALIDATED_SLOTS];
    uint64_t differ=v->image!=c->image;
    /* Scalar: the compares are independent; vectorized, they become one chain. */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__>=14
#pragma GCC novector
#endif
    for (unsigned r=0;r<n;++r) {
        uint64_t base=in[2*r],bytes=in[2*r+1];
        differ|=(base^v->words[2*r])|(bytes^v->words[2*r+1]);words[2*r]=base;words[2*r+1]=bytes;
    }
    f->status=f->ret_val=0;f->fuel_budget=budget;f->fuel_used=used;f->arg0=arg0;f->arg1=arg1;
    bool valid=used<=budget;
    if (valid && differ) {
        uint64_t regs[TISA_NUM_VREGS]={(uint64_t)(uintptr_t)f};
        valid=tisa_entry_valid(&c->source.prog,f,regs);
        if (valid) { v->image=c->image;memcpy(v->words,words,2*n*sizeof *words); }
    }
#if NM_COMPONENT_CHECK_VALIDATION
    { uint64_t regs[TISA_NUM_VREGS]={(uint64_t)(uintptr_t)f};
      if (valid!=tisa_entry_valid(&c->source.prog,f,regs)) abort(); }
#endif
    if (!valid) { *out=(NmComponentResult){TISA_OUTCOME_INVALID_INPUT,TISA_OUTCOME_INVALID_INPUT,used};return; }
    union { const void *code;TcIsaNativeFn fn; } entry={.code=c->image->entry};
    entry.fn(f,0,0,0,0,0);
    uint64_t status=f->status,payload=f->ret_val,fuel=f->fuel_used;
    __asm__("" : "+r"(status),"+r"(payload),"+r"(fuel));
    out->outcome=(TcIsaOutcome)status;out->payload=payload;out->fuel_used=fuel;
}
static void native_call(const NmComponent *c,const NmComponentSpan *spans,uint64_t arg0,uint64_t arg1,
                        uint64_t budget,uint64_t used,NmComponentResult *out) {
    uint64_t in[2*NM_COMPONENT_REGIONS];
    for (unsigned r=0;r<c->image->regions;++r) { in[2*r]=(uint64_t)(uintptr_t)spans[r].base;in[2*r+1]=spans[r].bytes; }
    native_run(c,in,arg0,arg1,budget,used,out);
}
NmCallStatus nm_component_call_scalar(const NmComponent *c,uint64_t arg0,uint64_t arg1,uint64_t budget,uint64_t used,
                                      NmComponentResult *result) {
    if (!c || !c->image || !result || c->image->regions || !c->has_native) return NM_CALL_BAD_ARGUMENT;
    native_run(c,NULL,arg0,arg1,budget,used,result);return NM_CALL_OK;
}
NmCallStatus nm_component_call_words(const NmComponent *c,const uint64_t *words,uint64_t budget,uint64_t used,
                                     NmComponentResult *result) {
    if (!c || !c->image || !words || !result || !c->has_native) return NM_CALL_BAD_ARGUMENT;
    native_run(c,words,0,0,budget,used,result);return NM_CALL_OK;
}
static NmComponentResult execute(const NmComponent *c,bool native,const NmComponentSpan *spans,
                                  uint64_t arg0,uint64_t arg1,uint64_t budget,uint64_t used) {
    TcIsaRuntimeFrame frame;frame_make(&frame,spans,c->image->regions,arg0,arg1,budget,used);
    uint64_t regs[TISA_NUM_VREGS]={0};regs[0]=(uint64_t)(uintptr_t)&frame;
    TcIsaX86 linked=c->native;linked.code=(void *)(uintptr_t)c->image->entry;
#if TISA_DEV_REFERENCE
    TcIsaOutcome outcome=native ? tisa_x86_run(&c->source.prog,&linked,&frame,regs) :
        tisa_run(&c->source.prog,&frame,regs);
#else
    (void)native;
    TcIsaOutcome outcome=tisa_x86_run(&c->source.prog,&linked,&frame,regs);
#endif
    return (NmComponentResult){outcome,frame.ret_val,frame.fuel_used};
}
#if TISA_DEV_REFERENCE
static bool copy_make(Copy *copy,NmComponentSpan source) {
    if (source.bytes>SIZE_MAX-2*GUARD) return false;
    copy->owner=malloc(source.bytes+2*GUARD);if (!copy->owner) return false;
    memset(copy->owner,GUARD_BYTE,source.bytes+2*GUARD);
    copy->span=(NmComponentSpan){copy->owner+GUARD,source.bytes,source.writable};
    if (source.bytes) memcpy(copy->span.base,source.base,source.bytes);
    return true;
}
static bool copy_guard(const Copy *copy) {
    for (unsigned i=0;i<GUARD;++i)
        if (copy->owner[i]!=GUARD_BYTE || copy->owner[GUARD+copy->span.bytes+i]!=GUARD_BYTE) return false;
    return true;
}
#endif
NmCallStatus nm_component_call(const NmComponent *c,NmExecution mode,const NmComponentSpan original[NM_COMPONENT_REGIONS],
                               uint64_t arg0,uint64_t arg1,uint64_t budget,uint64_t used,
                               NmComponentResult *result,char *error,size_t size) {
    if (!c || !c->image || !original || !result)
        return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"missing component, spans or result");
#if TISA_DEV_REFERENCE
    if (mode!=NM_REFERENCE && mode!=NM_NATIVE && mode!=NM_COMPARE)
        return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"invalid execution mode");
#else
    if (mode!=NM_NATIVE)
        return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"reference execution requires a development build");
#endif
    if (mode!=NM_REFERENCE && !c->has_native)
        return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"this program was opened for reference execution only");
    unsigned n=c->image->regions;
    if (n>NM_COMPONENT_REGIONS) return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"invalid component region count");
    for (unsigned r=0;r<n;++r) if (original[r].writable!=(c->image->modes[r]==2))
        return call_failed(NM_CALL_BAD_ARGUMENT,error,size,"component span access differs from its declared interface");
    if (error && size) error[0]=0;
    if (mode==NM_NATIVE) { native_call(c,original,arg0,arg1,budget,used,result);return NM_CALL_OK; }
    if (mode!=NM_COMPARE) { *result=execute(c,false,original,arg0,arg1,budget,used);return NM_CALL_OK; }
#if TISA_DEV_REFERENCE
    TcIsaRuntimeFrame entry;frame_make(&entry,original,n,arg0,arg1,budget,used);
    uint64_t regs[TISA_NUM_VREGS]={0};regs[0]=(uint64_t)(uintptr_t)&entry;
    if (!tisa_entry_valid(&c->source.prog,&entry,regs)) {
        *result=(NmComponentResult){TISA_OUTCOME_INVALID_INPUT,3,used};return NM_CALL_OK;
    }
    Copy ref[NM_COMPONENT_REGIONS]={0},nat[NM_COMPONENT_REGIONS]={0};
    NmComponentSpan rs[NM_COMPONENT_REGIONS]={0},ns[NM_COMPONENT_REGIONS]={0};bool ready=true;
    for (unsigned r=0;r<n && ready;++r) {
        ready=copy_make(ref+r,original[r]) && copy_make(nat+r,original[r]);
        if (ready) { rs[r]=ref[r].span;ns[r]=nat[r].span; }
    }
    if (!ready) {
        for (unsigned r=0;r<n;++r) { free(ref[r].owner);free(nat[r].owner); }
        return call_failed(NM_CALL_RESOURCE,error,size,"comparison storage allocation failed");
    }
    NmComponentResult a=execute(c,false,rs,arg0,arg1,budget,used),b=execute(c,true,ns,arg0,arg1,budget,used);
    bool same=a.outcome==b.outcome && a.payload==b.payload && a.fuel_used==b.fuel_used;
    for (unsigned r=0;r<n;++r) {
        same=same && copy_guard(ref+r) && copy_guard(nat+r) &&
            (!original[r].bytes || !memcmp(rs[r].base,ns[r].base,original[r].bytes));
        if (!original[r].writable && original[r].bytes)
            same=same && !memcmp(original[r].base,rs[r].base,original[r].bytes) &&
                !memcmp(original[r].base,ns[r].base,original[r].bytes);
    }
    if (same) for (unsigned r=0;r<n;++r)
        if (original[r].writable && original[r].bytes) memcpy(original[r].base,ns[r].base,original[r].bytes);
    for (unsigned r=0;r<n;++r) { free(ref[r].owner);free(nat[r].owner); }
    *result=b;return same ? NM_CALL_OK : call_failed(NM_CALL_DIFFERENCE,error,size,
        "native/reference result, region or memory-frame difference");
#else
    return NM_CALL_BAD_ARGUMENT;
#endif
}
