#ifndef TCALC_NATIVE_EMIT_H
#define TCALC_NATIVE_EMIT_H
#include "native_match.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { NmNode node;uint64_t source; } NmEmitNode;
typedef struct { uint64_t source,next,first,mode; } NmEmitFrame;
typedef struct {
    const NmNode *nodes;size_t nodes_count;
    const uint64_t *children;size_t children_count;
    const uint64_t *bindings;size_t variables_count;
    const NmVariableKey *keys;size_t keys_count;
    uint64_t root;
} NmEmitInput;
typedef struct {
    NmEmitNode *nodes;size_t nodes_count;
    uint64_t *children;size_t children_count;
    uint64_t *memo;size_t memo_words;
    NmEmitFrame *stack;size_t stack_count;
    uint64_t *state;size_t state_words;
} NmEmitWork;
typedef enum {
    NM_EMIT_FIRED=1,NM_EMIT_UNSUPPORTED=2,NM_EMIT_INVALID=3,NM_EMIT_EXHAUSTED=4
} NmEmitOutcome;
typedef struct {
    NmEmitOutcome outcome;
    uint64_t root,reason,fuel_used;
    size_t nodes_written,children_written,peak_stack,expanded;
} NmEmitResult;
typedef struct NmEmitProgram NmEmitProgram;

NmEmitProgram *nm_emit_program_default(bool native,char *error,size_t error_size);
NmEmitProgram *nm_emit_program_open(const char *path,bool native,char *error,size_t error_size);
void nm_emit_program_close(NmEmitProgram *program);
size_t nm_emit_state_words(const NmEmitProgram *program);
const uint8_t *nm_emit_native_bytes(const NmEmitProgram *program,size_t *size);

/* Instantiate the reachable graph under fixed, acyclic bindings. Unbound
 * scoped keys are preserved; no generative renaming or activation occurs.
 * Output nodes have postorder child ids and retain a source-node origin.
 * A tuple of output roots shares one memo across the complete bundle.
 * Inputs and their owners remain stable throughout the call. All work spans
 * are caller-owned and mutually disjoint. Publish only after FIRED.
 * Failure retains private prefix counts and consumed fuel. Node zero is
 * reserved. Memo needs nodes_count+variables_count words; state size is
 * obtained from the loaded component. */
NmCallStatus nm_emit_call(const NmEmitProgram *program,NmExecution execution,
                          const NmEmitInput *input,const NmEmitWork *work,
                          uint64_t fuel_budget,uint64_t fuel_used,NmEmitResult *result,
                          char *error,size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
