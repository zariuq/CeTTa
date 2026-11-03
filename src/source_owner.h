#ifndef CETTA_SOURCE_OWNER_H
#define CETTA_SOURCE_OWNER_H
#include "space.h"
typedef struct CettaOccurrenceSource CettaOccurrenceSource;
typedef struct CettaCandidateCursor CettaCandidateCursor;
typedef enum {
    CETTA_CANDIDATE_OK,CETTA_CANDIDATE_ROW,CETTA_CANDIDATE_PROGRESS,
    CETTA_CANDIDATE_END,CETTA_CANDIDATE_CANCELLED,CETTA_CANDIDATE_STALE,
    CETTA_CANDIDATE_INVALID,CETTA_CANDIDATE_INVALIDATED,
    CETTA_CANDIDATE_INCOMPLETE,CETTA_CANDIDATE_CAPACITY,
    CETTA_CANDIDATE_LIMIT,CETTA_CANDIDATE_UNSUPPORTED
} CettaCandidateStatus;
/* A captured source owns a complete original-candidate prototype. It carries
 * neither publisher authority nor a freshness assertion about that publisher.
 * Candidate selection must preserve a complete superset, never instantiate
 * stored facts. Source/cursor children remain valid after parent release. */
CettaCandidateStatus cetta_occurrence_source_open(Space *,size_t maximum_bytes,
    CettaOccurrenceSource **);
/* One original portable fact and its saved ground identity. This historical
 * source grants no publication capability and uses the ordinary matcher. */
CettaCandidateStatus cetta_occurrence_source_single(Atom *fact,Atom *identity,
    size_t maximum_bytes,CettaOccurrenceSource **);
void cetta_occurrence_source_retain(CettaOccurrenceSource *);
void cetta_occurrence_source_release(CettaOccurrenceSource *);
uint64_t cetta_occurrence_source_count(const CettaOccurrenceSource *);
size_t cetta_occurrence_source_limit(const CettaOccurrenceSource *);
void cetta_occurrence_source_storage(const void *,CettaStorageCensus *);
/* Turn the retained original-candidate prototype into an ordinary read-only
 * Space. The initialized empty target independently retains the source. */
bool cetta_occurrence_source_space(CettaOccurrenceSource *,Space *target);
CettaCandidateStatus cetta_candidate_cursor_open(CettaOccurrenceSource *,
    Atom *pattern,CettaCandidateCursor **);
CettaCandidateStatus cetta_candidate_cursor_clone(const CettaCandidateCursor *,
    bool rewind,CettaCandidateCursor **);
typedef struct {
    uint64_t sequence,body_bytes,identity_bytes;
} CettaCandidateRow;
/* Each admitted next advances at most one provider transition. Same last
 * sequence replays the immutable packets; later/older sequences refuse.
 * Bodies and identities are separate CAV1 packets, with no cross-fact variable
 * scope. A packet may be read in chunks; frame size never truncates a fact. */
CettaCandidateStatus cetta_candidate_cursor_next(CettaCandidateCursor *,
    uint64_t sequence,uint64_t work,CettaCandidateRow *);
CettaCandidateStatus cetta_candidate_cursor_read(const CettaCandidateCursor *,
    uint64_t sequence,bool identity,uint64_t offset,size_t maximum_bytes,
    const unsigned char **bytes,size_t *size);
CettaCandidateStatus cetta_candidate_cursor_cancel(CettaCandidateCursor *);
uint64_t cetta_candidate_cursor_sequence(const CettaCandidateCursor *);
void cetta_candidate_cursor_storage(const void *,CettaStorageCensus *);
void cetta_candidate_cursor_close(CettaCandidateCursor *);
const char *cetta_candidate_status_name(CettaCandidateStatus);
#endif
