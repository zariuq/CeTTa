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
bool nm_component_span(NmComponentSpan *span,const void *pointer,size_t count,size_t width,bool writable);
NmCallStatus nm_component_call(const NmComponent *component,NmExecution execution,
                               const NmComponentSpan spans[NM_COMPONENT_REGIONS],uint64_t arg0,uint64_t arg1,
                               uint64_t budget,uint64_t used,NmComponentResult *result,char *error,size_t size);
#endif
