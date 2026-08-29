#ifndef CETTA_SEARCH_CONTROL_ADVICE_H
#define CETTA_SEARCH_CONTROL_ADVICE_H

#include "search_machine.h"

#include <pthread.h>

/* Bounded derivation-path whistle.  Homeomorphic embedding is advisory: a
 * positive result may influence a selector, but never removes, refuses, or
 * completes an occurrence.  Terms retained by the whistle are owned copies.
 * Parent occurrence 0 denotes a derivation root. */
#define CETTA_WHISTLE_ANCESTOR_CAPACITY 32u
#define CETTA_WHISTLE_DEMOTION_CAPACITY 64u
#define CETTA_WHISTLE_EMBEDDING_WORK_LIMIT 4096u
#define CETTA_WHISTLE_EMBEDDING_DEPTH_LIMIT 256u

bool cetta_atom_homeomorphically_embedded(
    Atom *ancestor, Atom *descendant);

typedef struct {
    Arena *term_arenas[CETTA_WHISTLE_ANCESTOR_CAPACITY];
    Atom *terms[CETTA_WHISTLE_ANCESTOR_CAPACITY];
    uint64_t term_occurrences[CETTA_WHISTLE_ANCESTOR_CAPACITY];
    uint64_t term_parents[CETTA_WHISTLE_ANCESTOR_CAPACITY];
    size_t term_begin;
    size_t term_length;
    uint64_t demoted[CETTA_WHISTLE_DEMOTION_CAPACITY];
    size_t demoted_length;
    uint64_t whistles;
    pthread_mutex_t mutex;
    bool initialized;
} CettaWhistle;

bool cetta_whistle_init(CettaWhistle *whistle);
void cetta_whistle_destroy(CettaWhistle *whistle);
bool cetta_whistle_observe(
    CettaWhistle *whistle, Atom *term, uint64_t occurrence,
    uint64_t parent_occurrence);
bool cetta_whistle_demoted(
    CettaWhistle *whistle, uint64_t occurrence);
uint64_t cetta_whistle_count(CettaWhistle *whistle);

/* The ambient monitor copies every accepted submission before returning, so
 * worker progress never depends on an evaluator arena remaining live.  A
 * full ring conservatively drops advice.  Stop drains accepted work.  The
 * observed whistle must outlive the monitor: stop the monitor before
 * destroying the whistle.  Callers must cease submission before stop begins.
 */
#define CETTA_WHISTLE_MONITOR_RING_CAPACITY 128u

typedef struct {
    Arena *arena;
    Atom *term;
    uint64_t occurrence;
    uint64_t parent_occurrence;
} CettaWhistleObservation;

typedef struct {
    CettaWhistle *whistle;
    CettaWhistleObservation
        ring[CETTA_WHISTLE_MONITOR_RING_CAPACITY];
    size_t ring_begin;
    size_t ring_length;
    bool running;
    bool initialized;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
} CettaWhistleMonitor;

bool cetta_whistle_monitor_start(
    CettaWhistleMonitor *monitor, CettaWhistle *whistle);
bool cetta_whistle_monitor_submit(
    CettaWhistleMonitor *monitor, Atom *term, uint64_t occurrence,
    uint64_t parent_occurrence);
void cetta_whistle_monitor_stop(CettaWhistleMonitor *monitor);

/* Local, explicitly selected control advice.  The key includes the active
 * evaluator identity and Space revision as well as the structural query hash.
 * A 32-bit hash remains a hint rather than proof of program identity; advice
 * therefore never gains semantic authority. */
typedef enum {
    CETTA_ACT_OUTCOME_COMPLETE = 0,
    CETTA_ACT_OUTCOME_INCOMPLETE,
    CETTA_ACT_OUTCOME_REFUSED,
} CettaActOutcome;

typedef struct {
    uint32_t language_id;
    uint32_t profile_id;
    uint64_t space_revision;
    uint32_t program_hash;
} CettaActKey;

typedef struct {
    CettaActKey key;
    char policy[24];
    CettaActOutcome outcome;
    uint64_t transitions;
    uint64_t answers;
    uint64_t expansions;
    uint64_t max_frontier;
    uint64_t whistles;
} CettaActRecord;

/* Atomically replace the one current record for KEY.  The profile directory
 * must already exist. */
bool cetta_act_profile_store(
    const char *directory, const CettaActRecord *record);
bool cetta_act_profile_lookup(
    const char *directory, const CettaActKey *key,
    CettaActRecord *record);
/* Reuse a policy that emitted an answer.  Escalate to the recurrent-oldest
 * discipline only after an explicitly incomplete zero-answer run.  Finite
 * zero-answer closure and refusal produce no advice. */
bool cetta_act_profile_choose(
    const char *directory, const CettaActKey *key,
    CettaSearchControllerPolicy *policy,
    CettaSelectionAutomaton *automaton);

/* Incremental conditional compression
 * -----------------------------------
 *
 * This is local control advice, not a semantic compressor and not pruning
 * authority.  The bounded dictionary contains projections on bounded causal
 * lineages that ended in an answer.  A candidate is preferred when the same
 * fixed decoder can describe its projection more compactly relative to that
 * successful dictionary.  The model is revision/query scoped by CettaActKey
 * and projection scoped by `projection_identity`.
 *
 * A ranking always remains a complete stable permutation.  Empty models,
 * projection mismatch, corrupt persistence, overflow, or unavailable traces
 * decline to the controller's deterministic source order. */
#define CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY ((size_t)8192u)
#define CETTA_ACT_COMPRESSION_SCORER_IDENTITY \
    UINT64_C(0x49434f4d505631) /* "ICOMPv1" */

typedef struct {
    CettaActKey key;
    uint64_t projection_identity;
    uint64_t revision;
    uint64_t observations;
    uint64_t digest;
    size_t dictionary_length;
    uint8_t dictionary[CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY];
} CettaActCompressionModel;

typedef struct {
    uint64_t ordinary_bits;
    uint64_t feature_bits;
    uint64_t residual_bits;
    uint64_t saved_bits;
} CettaActCompressionMeasurement;

typedef struct {
    const CettaActCompressionModel *model;
    uint64_t attempts;
    uint64_t applied;
    uint64_t deferred;
    uint64_t invalidated;
    size_t maximum_working_bytes;
} CettaActCompressionRankContext;

#define CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY ((size_t)256u)

typedef struct {
    uint64_t occurrence_id;
    uint64_t parent_occurrence_id;
    CettaContinuationTrace trace;
} CettaActCompressionLineageEntry;

typedef struct {
    CettaActCompressionLineageEntry
        entries[CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY];
    size_t begin;
    size_t length;
} CettaActCompressionLineage;

bool cetta_act_compression_model_init(
    CettaActCompressionModel *model,
    const CettaActKey *key);
bool cetta_act_compression_model_lookup(
    const char *directory, const CettaActKey *key,
    CettaActCompressionModel *model);
bool cetta_act_compression_model_store(
    const char *directory,
    const CettaActCompressionModel *model);
bool cetta_act_compression_model_observe_success(
    CettaActCompressionModel *model,
    const CettaContinuationTrace *trace);
void cetta_act_compression_lineage_init(
    CettaActCompressionLineage *lineage);
void cetta_act_compression_lineage_destroy(
    CettaActCompressionLineage *lineage);
size_t cetta_act_compression_lineage_resident_bytes(
    const CettaActCompressionLineage *lineage);
/* Successful record consumes TRACE. */
bool cetta_act_compression_lineage_record(
    CettaActCompressionLineage *lineage,
    uint64_t occurrence_id,
    uint64_t parent_occurrence_id,
    CettaContinuationTrace *trace);
/* Credit the retained root-to-answer path ending at OCCURRENCE_ID. */
bool cetta_act_compression_lineage_credit_answer(
    CettaActCompressionLineage *lineage,
    uint64_t occurrence_id,
    CettaActCompressionModel *model,
    size_t *updates);
bool cetta_act_compression_measure(
    const CettaActCompressionModel *model,
    const CettaContinuationTrace *trace,
    CettaActCompressionMeasurement *measurement);
void cetta_act_compression_ranker_init(
    const CettaActCompressionModel *model,
    CettaActCompressionRankContext *context,
    CettaControllerBatchRanker *ranker);

#endif /* CETTA_SEARCH_CONTROL_ADVICE_H */
