#ifndef TCALC_NATIVE_COMPONENT_H
#define TCALC_NATIVE_COMPONENT_H
#include "native_match.h"
#include "p1_load.h"
#include "tisa_x86.h"

enum { NM_COMPONENT_REGIONS=9 };
typedef struct {
    const char *name,*image_error,*profile_error;
    const uint8_t *entry,*body,*body_end,*expected,*expected_end;
    const char *text,*text_end;
    uint8_t regions,modes[NM_COMPONENT_REGIONS];
} NmComponentImage;
typedef struct {
    P1Program source;
    TcIsaX86 native;
    const NmComponentImage *image;
    bool has_native;
} NmComponent;
typedef struct { uint8_t *base;size_t bytes;bool writable; } NmComponentSpan;
typedef struct { TcIsaOutcome outcome;uint64_t payload,fuel_used; } NmComponentResult;

bool nm_component_init(NmComponent *component,const NmComponentImage *image,const char *path,
                        bool native,char *error,size_t size);
void nm_component_dispose(NmComponent *component);
const uint8_t *nm_component_bytes(const NmComponent *component,size_t *size);
static inline bool nm_component_span(NmComponentSpan *span,const void *pointer,size_t count,size_t width,bool writable) {
    if (!width || count>SIZE_MAX/width || (count && !pointer)) return false;
    *span=(NmComponentSpan){(uint8_t *)(uintptr_t)pointer,count*width,writable};return true;
}
NmCallStatus nm_component_call(const NmComponent *component,NmExecution execution,
                               const NmComponentSpan spans[NM_COMPONENT_REGIONS],uint64_t arg0,uint64_t arg1,
                               uint64_t budget,uint64_t used,NmComponentResult *result,char *error,size_t size);
/* A native call whose region words the caller gives: base and byte length
 * of each region, in order, as they go into the frame. */
NmCallStatus nm_component_call_words(const NmComponent *component,const uint64_t *words,uint64_t budget,uint64_t used,
                                     NmComponentResult *result);
/* A native call of a component without regions, on two scalar arguments. */
NmCallStatus nm_component_call_scalar(const NmComponent *component,uint64_t arg0,uint64_t arg1,
                                      uint64_t budget,uint64_t used,NmComponentResult *result);
#endif
