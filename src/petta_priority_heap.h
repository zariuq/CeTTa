#ifndef CETTA_PETTA_PRIORITY_HEAP_H
#define CETTA_PETTA_PRIORITY_HEAP_H

#include "atom.h"

typedef enum {
    PETTA_PRIORITY_HEAP_DECLINED = 0,
    PETTA_PRIORITY_HEAP_ANSWER,
    PETTA_PRIORITY_HEAP_NO_ANSWER,
    PETTA_PRIORITY_HEAP_CAPACITY,
} PettaPriorityHeapStatus;

typedef struct {
    Atom *result;
    Atom *key;
    Atom *value;
} PettaPriorityHeapAnswer;

/* Realize the ground finite fragment of SWI library(heaps)' persistent
 * pairing heaps. The exposed heap/t/list representation and equal-key LIFO
 * order are preserved. Calls outside that fragment decline to the imported
 * predicate. */
PettaPriorityHeapStatus petta_priority_heap_call(
    Arena *arena, Atom *resolved_call, PettaPriorityHeapAnswer *answer);
/* Validate untrusted finite data at an import/resumption boundary. Normal
 * operations preserve the invariant of a heap returned by this library. */
PettaPriorityHeapStatus petta_priority_heap_validate(Atom *heap,
    bool (*interrupted)(void), bool *valid);

#endif
