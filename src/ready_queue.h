#ifndef CETTA_READY_QUEUE_H
#define CETTA_READY_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct CettaReadyQueue CettaReadyQueue;
typedef struct CettaReadyLink CettaReadyLink;

/* Intrusive host scheduling order. Links have stable addresses and start
 * zero-initialized. One owner thread changes a queue; these are not evaluator
 * capabilities or persistent identities. The caller retains the actual task,
 * its durable identity, and any waiting continuation. */
struct CettaReadyLink {
    CettaReadyLink *previous, *next;
    CettaReadyQueue *owner;
};
struct CettaReadyQueue {
    CettaReadyLink *first, *last;
    size_t count, capacity;
};

typedef enum {
    READY_ADMITTED, READY_PRESENT, READY_FULL, READY_FOREIGN, READY_INVALID
} CettaReadyAdmission;

/* Initialize an unused queue as {.capacity = limit}. Admission never evicts
 * another task; repeated admission of the same link leaves its place intact.
 * A full queue requires backpressure at the authoritative source. */
CettaReadyAdmission cetta_ready_admit(CettaReadyQueue *queue, CettaReadyLink *task);

/* Return the oldest link and move it to the tail, in constant time. Call once
 * per scheduling opportunity, including a blocked visit. Arrivals go behind
 * existing waiters; completion may remove the returned link. Thus a task
 * remaining queued is visited within its current prefix length + 1, even
 * when other tasks retire and new tasks arrive between visits. This bounds
 * visits, not elapsed time or task completion. */
CettaReadyLink *cetta_ready_rotate(CettaReadyQueue *queue);

/* Remove only this membership. False means absent/from a different queue.
 * Removing a ready entry never cancels its transport or deletes durable work.
 * All links must be removed before their queue or containing objects die. */
bool cetta_ready_remove(CettaReadyQueue *queue, CettaReadyLink *task);
void cetta_ready_clear(CettaReadyQueue *queue);

#endif
