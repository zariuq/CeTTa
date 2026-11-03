#ifndef CETTA_PROOF_PRODUCER_H
#define CETTA_PROOF_PRODUCER_H
#include "native_query.h"

#define CETTA_PROOF_PATH_MAX 16u
#define CETTA_PROOF_SOURCE_MAX 8u

typedef struct {
    uint8_t length;
    CettaExprIndex child[CETTA_PROOF_PATH_MAX];
} CettaProofFieldPath;
typedef struct {
    size_t source_leg;
    CettaProofFieldPath rank, proof;
} CettaProofProducerLayout;

typedef enum {
    CETTA_PROOF_PRODUCER_DECLINED = 0,
    CETTA_PROOF_PRODUCER_SHAPE_ONLY,
    CETTA_PROOF_PRODUCER_CAPACITY,
} CettaProofProducerStatus;
typedef enum {
    CETTA_PROOF_REASON_NONE = 0,
    CETTA_PROOF_REASON_QUERY,
    CETTA_PROOF_REASON_LAYOUT,
    CETTA_PROOF_REASON_SPACE,
    CETTA_PROOF_REASON_FRAGMENT,
    CETTA_PROOF_REASON_RANK,
    CETTA_PROOF_REASON_PROOF,
    CETTA_PROOF_REASON_LABEL,
    CETTA_PROOF_REASON_STALE,
    CETTA_PROOF_REASON_CAPACITY,
} CettaProofProducerReason;

enum {
    CETTA_PROOF_NEEDS_SOURCE_AUTHORITY = 1u << 0,
    CETTA_PROOF_NEEDS_SINGLE_CLOSED_SEED = 1u << 1,
    CETTA_PROOF_NEEDS_FUNCTIONAL_BATCH = 1u << 2,
    CETTA_PROOF_NEEDS_WORD_SET_UNAMBIGUITY = 1u << 3,
    CETTA_PROOF_NEEDS_LABEL_DATA_AUTHORITY = 1u << 4,
    CETTA_PROOF_NEEDS_PUBLICATION_PREFLIGHT = 1u << 5,
    CETTA_PROOF_NEEDS_WORK_ADMISSION = 1u << 6,
    CETTA_PROOF_NEEDS_OWNER_ADMISSION = 1u << 7,
    CETTA_PROOF_NEEDS_GROUND_LABEL_BINDING = 1u << 8,
};

typedef struct CettaProofProducerPlan CettaProofProducerPlan;

typedef struct {
    SpaceReadToken read;
    CettaCount count;
    uint64_t universe_instance, universe_epoch;
    bool has_lifetime_hold;
    bool hold_keeps_storage;
} CettaProofSourceBinding;
typedef struct {
    CettaProofProducerLayout layout;
    /* These full original arities accompany structural field omission.
     * Erased Atom keys from different layouts must never be interchanged. */
    CettaExprLen proof_ancestor_arity[CETTA_PROOF_PATH_MAX];
    SpaceProgramToken program;
    CettaProofSourceBinding sources[CETTA_PROOF_SOURCE_MAX], target;
    size_t source_count;
    VarId proof_variable;
    int64_t source_rank, target_rank;
    uint64_t source_root;
    uint32_t source_position;
    uint32_t remaining;
    bool count_only;
    bool literal_label;
    bool program_has_lifetime_hold;
} CettaProofProducerDescription;

/* A source-shape capture only. Layout is an untrusted proposal; the actual
 * query, modes, variable uses and all source/target identities are checked.
 * A SHAPE_ONLY result never licenses execution or publication. A decline or
 * capacity refusal leaves *out unchanged and makes no source/target effect.
 * program and all query Spaces must be live for this synchronous capture. */
CettaProofProducerStatus cetta_proof_producer_prepare(
    const CettaNativeQuery *query, Space *program,
    uint64_t source_root, uint32_t source_position,
    const CettaProofProducerLayout *layout,
    CettaProofProducerPlan **out, CettaProofProducerReason *why);
void cetta_proof_producer_free(CettaProofProducerPlan *plan);
bool cetta_proof_producer_describe(const CettaProofProducerPlan *plan,
                                  CettaProofProducerDescription *out);
/* Owned immutable syntax until plan release. State templates structurally
 * omit the proof child at the fixed layout, preserving all remaining
 * aliases. Canonicalize each complete template once, never per field. */
const Atom *cetta_proof_producer_state_template(
    const CettaProofProducerPlan *plan, bool output);
/* Type exactness only; this does not license evaluating an application. */
bool cetta_proof_producer_exact_label(const Atom *value);
const Atom *cetta_proof_producer_label(const CettaProofProducerPlan *plan);
const Atom *cetta_proof_producer_pattern(const CettaProofProducerPlan *plan, size_t leg);
const Atom *cetta_proof_producer_effect(const CettaProofProducerPlan *plan);
const Atom *cetta_proof_producer_answer(const CettaProofProducerPlan *plan);
/* Compare against caller-owned live Spaces, never a raw retained pointer.
 * This validates captured identities/counts, not the remaining obligations. */
bool cetta_proof_producer_is_current(const CettaProofProducerPlan *plan,
    Space *program, Space *const *live_sources, size_t source_count,
    Space *target);
#endif
