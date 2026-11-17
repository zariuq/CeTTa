#ifndef TCALC_NATIVE_OUTPUT_H
#define TCALC_NATIVE_OUTPUT_H
#include "native_emit.h"
#include "native_query.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NM_OUTPUT_READY,NM_OUTPUT_EXHAUSTED,NM_OUTPUT_RESOURCE,NM_OUTPUT_DEFER,
    NM_OUTPUT_INVALIDATED,NM_OUTPUT_INTERRUPTED,NM_OUTPUT_FAULT
} NmOutputStatus;
typedef struct {
    NmImportLimits import;
    size_t nodes,children,stack;
} NmOutputLimits;
typedef struct {
    NmOutputStatus status;
    NmImportStats import;
    bool activated,has_core;
    uint64_t fuel_used;
    NmEmitResult core;
} NmOutputReceipt;
typedef struct NmOutput NmOutput;
typedef struct {
    const NmEmitNode *nodes;size_t nodes_count;
    const uint64_t *children;size_t children_count;
    const uint64_t *roots;size_t roots_count;
} NmOutputGraph;

/* Instantiate all templates under one fixed view and substitution. A solution
 * view may supply that substitution; it must stay alive through this call.
 * Success owns a materialized graph, with one memo shared across the bundle.
 * Repeated outputs remain repeated roots. Unbound variable keys retain their
 * identities. This is neither generative row freshening nor a complete branch
 * snapshot: the host retains the complete answer context separately.
 *
 * One activation fuel unit precedes import, in addition to the native receipt.
 * Import limits bound callbacks and table sizes independently. Failures return
 * *out NULL and retain consumed fuel and private prefix counts in the receipt.
 * There is no public store effect, evaluation, or resumable partial output.
 * Empty template bundles are supported; they do not establish query completion.
 *
 * The source view is checked before import, before execution and after it.
 * Source syntax owners and the pin callback context remain borrowed until
 * release. The source's inspect/child/binding callback context is not retained
 * or invoked after construction. Program and templates may then be released.
 * Before publishing, the host must revalidate its own view/commit token. */
NmOutputStatus nm_output_build(const NmEmitProgram *program,NmExecution execution,
    NmQueryView view,const NmHostValue *templates,size_t count,NmOutputLimits limits,
    uint64_t fuel_budget,uint64_t fuel_used,NmOutput **out,NmOutputReceipt *receipt,
    char *error,size_t error_size);
/* Borrowed immutable graph, available only while the pin remains valid. Node
 * zero is reserved; all child ids precede their parent. Variable node.a is the
 * import's dense slot; node.b/node.c carry the full variable/scope identity. */
const NmOutputGraph *nm_output_graph(const NmOutput *output);
/* The original source label of an emitted subterm. It denotes the source
 * before instantiation, not an already materialized host value. The synthetic
 * bundle node has no label. Zero-valued source encodings remain valid. */
bool nm_output_origin(const NmOutput *output,uint64_t node,NmHostValue *origin);
void nm_output_free(NmOutput *output);

#ifdef __cplusplus
}
#endif
#endif
