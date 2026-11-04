#ifndef CETTA_PROOF_LANGUAGE_REGION_H
#define CETTA_PROOF_LANGUAGE_REGION_H
#include "term_universe.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Owned proof-language component boundary. The graph owns its arrays and registered ID roots.
 * The caller's TermUniverse must physically outlive every image/cursor; root
 * registration does not retain that external object or its symbol session.
 * Builders and individual cursors are thread-confined. Images are immutable.
 * The caller serializes TU mutation/collection at its usual safe boundaries.
 * Explicit TU storage reset invalidates borrowed IDs, even when the object
 * stays alive. Ordinary rooted collection does not invalidate the image.
 * No source compiler, source-fuel debit or Space mutation is implemented here. */
typedef struct CettaProofBuilder CettaProofBuilder;
typedef struct CettaProofImage CettaProofImage;
typedef struct CettaProofCursor CettaProofCursor;
#define CETTA_PROOF_NO_STATE UINT32_MAX
#define CETTA_PROOF_ALL_STATES UINT32_MAX

typedef enum {
    CETTA_PROOF_OK, CETTA_PROOF_DUPLICATE, CETTA_PROOF_AMBIGUOUS,
    CETTA_PROOF_CAPACITY, CETTA_PROOF_INVALIDATED
} CettaProofStatus;
typedef struct {
    uint32_t max_states, max_edges, max_rank;
    uint64_t max_support, max_producer_occurrences;
} CettaProofLimits;
typedef struct {
    uint32_t state, rank;
    AtomId key;
    uint64_t support;
} CettaProofStateView;
typedef struct {
    uint32_t edge;
    bool support_changed;
    uint64_t added_support;
    /* One offer produces a batch over the settled parent support. A duplicate
     * edge still reports that batch; it never multiplies future stored rows. */
    uint64_t producer_occurrences;
} CettaProofOffer;
typedef struct {
    uint32_t states, edges, max_rank;
    uint64_t support, producer_offers, producer_occurrences;
} CettaProofStats;

/* Exactly one rank-zero seed. Keys are complete alpha-canonical state terms
 * supplied by the domain; numeric IDs are not a replacement for that law.
 * Graph rank is generation from the seed (0,1,...), not decreasing BFC depth.
 * builder_state creates/looks up the NEXT layer; advance settles it.
 * Labels are exact ground symbols or native int scalars, never floats/native
 * resource handles. Inputs stay caller-owned: successful calls acquire
 * independent ID roots and never transfer the caller's ownership. */
CettaProofStatus cetta_proof_builder_new(TermUniverse *, AtomId root_key,
    AtomId seed, CettaProofLimits, CettaProofBuilder **out);
CettaProofStatus cetta_proof_builder_state(CettaProofBuilder *, AtomId key,
    uint32_t *out_state);
/* Parents belong to the current settled layer; children to its next layer.
 * Same parent/label/child is DUPLICATE support, with explicit producer returns.
 * Same parent/label and a different child is AMBIGUOUS and prevents sealing. */
CettaProofStatus cetta_proof_builder_offer(CettaProofBuilder *, uint32_t parent,
    AtomId label, uint32_t child, CettaProofOffer *out);
CettaProofStatus cetta_proof_builder_advance(CettaProofBuilder *);
bool cetta_proof_builder_layer(const CettaProofBuilder *, uint32_t *first,
    uint32_t *past_last);
bool cetta_proof_builder_get(const CettaProofBuilder *, uint32_t,
    CettaProofStateView *out);
/* Seal freezes the builder and returns one independent image reference. The
 * builder retains its reference until free; cursors can outlive both handles.
 * Allocation/overflow/refusal never publishes an image. A poisoned builder
 * may only be destroyed. */
CettaProofStatus cetta_proof_builder_seal(CettaProofBuilder *, CettaProofImage **out);
void cetta_proof_builder_free(CettaProofBuilder *);
bool cetta_proof_image_retain(CettaProofImage *);
void cetta_proof_image_release(CettaProofImage *);
CettaProofStats cetta_proof_image_stats(const CettaProofImage *);
/* Caller supplies the live borrowed universe; no stale raw-TU dereference. */
bool cetta_proof_image_matches_universe(const CettaProofImage *, const TermUniverse *);
size_t cetta_proof_image_retained_bytes(const CettaProofImage *);
typedef struct { uint32_t parent, child; AtomId label; } CettaProofEdgeView;
bool cetta_proof_image_edge(const CettaProofImage *, uint32_t, CettaProofEdgeView *out);
bool cetta_proof_image_state(const CettaProofImage *, uint32_t,
    CettaProofStateView *out);

/* Limits count physical graph microsteps, not source-language fuel. A consumer
 * rejecting rows shares ONE in/out budget across peeks and commits in a turn.
 * peek is bounded; commit is a separate allocation-free constant-time action. */
typedef struct { uint64_t remaining, visited; } CettaProofBudget;
typedef enum {
    CETTA_PROOF_ROW, CETTA_PROOF_PAUSED, CETTA_PROOF_END,
    CETTA_PROOF_CURSOR_INVALIDATED
} CettaProofNext;
typedef struct {
    AtomId state_key, seed;
    const AtomId *outer_labels;
    uint32_t label_count, state;
    uint64_t token;
} CettaProofWord;
CettaProofStatus cetta_proof_cursor_new(CettaProofImage *, uint32_t selected_state,
    CettaProofCursor **out);
CettaProofStatus cetta_proof_cursor_clone(const CettaProofCursor *, CettaProofCursor **out);
/* ROW remains pending: materialization/refusal cannot silently lose it. The
 * borrowed labels last through PAUSED/repeated peek until commit/free or TU
 * storage invalidation; they
 * are not the full SpaceOccurrenceCursor lent-AtomId contract. No generated
 * proof AtomId is cached or promised by this component. Labels are newest first. */
CettaProofNext cetta_proof_cursor_peek(CettaProofCursor *, CettaProofBudget *,
    CettaProofWord *out);
bool cetta_proof_cursor_commit(CettaProofCursor *, uint64_t token);
void cetta_proof_cursor_free(CettaProofCursor *);
#endif
