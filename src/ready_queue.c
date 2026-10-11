#include "ready_queue.h"

CettaReadyAdmission cetta_ready_admit(CettaReadyQueue *queue, CettaReadyLink *task) {
    if (!queue || !task) return READY_INVALID;
    if (task->owner) return task->owner == queue ? READY_PRESENT : READY_FOREIGN;
    if (queue->count >= queue->capacity) return READY_FULL;
    task->owner = queue;
    task->previous = queue->last;
    task->next = NULL;
    if (queue->last) queue->last->next = task;
    else queue->first = task;
    queue->last = task;
    ++queue->count;
    return READY_ADMITTED;
}

CettaReadyLink *cetta_ready_rotate(CettaReadyQueue *queue) {
    if (!queue || !queue->first) return NULL;
    CettaReadyLink *task = queue->first;
    if (task->next) {
        queue->first = task->next;
        queue->first->previous = NULL;
        task->next = NULL;
        task->previous = queue->last;
        queue->last->next = task;
        queue->last = task;
    }
    return task;
}

bool cetta_ready_remove(CettaReadyQueue *queue, CettaReadyLink *task) {
    if (!queue || !task || task->owner != queue) return false;
    if (task->previous) task->previous->next = task->next;
    else queue->first = task->next;
    if (task->next) task->next->previous = task->previous;
    else queue->last = task->previous;
    --queue->count;
    *task = (CettaReadyLink){0};
    return true;
}

void cetta_ready_clear(CettaReadyQueue *queue) {
    if (queue) while (queue->first) cetta_ready_remove(queue, queue->first);
}
