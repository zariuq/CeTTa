#ifndef CETTA_PROOF_ROW_H
#define CETTA_PROOF_ROW_H
#include "proof_producer.h"
#include "proof_language_region.h"

#define CETTA_PROOF_ROW_LABEL_LIMIT 64u
#define CETTA_PROOF_ROW_CHILD_LIMIT 4096u

typedef enum {
    CETTA_PROOF_ROW_READY,
    CETTA_PROOF_ROW_DECLINED,
    CETTA_PROOF_ROW_CAPACITY
} CettaProofRowStatus;
typedef struct {
    uint32_t labels;
    uint32_t constructors;
    uint32_t child_slots;
} CettaProofRowWork;
/* On true the destination has retained/listed ID under its existing root
 * provider before collection is enabled again. False leaves that owner
 * unchanged. This callback only retains data; it makes no Space effect. */
typedef bool (*CettaProofRowRetain)(void *context, TermUniverse *, AtomId id);
typedef struct CettaProofRowLayout {
    CettaProofFieldPath proof;
    CettaExprLen arity[CETTA_PROOF_PATH_MAX];
    SymbolId head[CETTA_PROOF_PATH_MAX];
    uint64_t universe_instance, storage_epoch;
} CettaProofRowLayout;
/* Copies only the stored data layout. No plan, mutable Space or program
 * lifetime is retained. The symbol session and TU remain external borrows. */
bool cetta_proof_row_layout_capture(const CettaProofProducerPlan *, CettaProofRowLayout *out);

/* Pure data construction, not producer execution or source certification.
 * Layout, word, external symbol session and TU stay live throughout this call.
 * The caller serializes TU access and starts at a safe collection boundary;
 * inhibit does not itself wait for an already running collector.
 * The fragment admits at most 64 labels and 4096 copied child slots. It
 * makes no claim about intern-table probes or source-language fuel.
 * On READY, *out is borrowed from the retaining destination until release.
 * On refusal, *out is unchanged and the cursor's pending word is untouched.
 * Commit that cursor only after the caller has adopted/delivered the row. */
CettaProofRowStatus cetta_proof_row_materialize(
    TermUniverse *universe, const CettaProofRowLayout *layout,
    const CettaProofWord *word, CettaProofRowRetain retain, void *context,
    AtomId *out, CettaProofRowWork *work);
#endif
