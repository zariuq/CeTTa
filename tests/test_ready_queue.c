#include "ready_queue.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TASKS = 16 };
typedef struct { CettaReadyLink link; unsigned id; } Task;

static void validate(CettaReadyQueue *q) {
    size_t count=0;
    CettaReadyLink *previous=NULL;
    for (CettaReadyLink *p=q->first;p;p=p->next) {
        assert(p->owner==q && p->previous==previous);
        previous=p;
        assert(++count<=q->capacity);
    }
    assert(count==q->count && previous==q->last);
    assert((q->first==NULL)==(q->last==NULL));
    if (q->last) assert(q->last->next==NULL);
}

static int trace(void) {
    CettaReadyQueue q={0};
    Task tasks[TASKS]={0};
    for (unsigned i=0;i<TASKS;++i) tasks[i].id=i;
    char line[80], operation[16], extra;
    unsigned id;
    while (fgets(line,sizeof(line),stdin)) {
        if (sscanf(line,"%15s %u %c",operation,&id,&extra)!=2) return 2;
        int status=0, visited=-1;
        if (!strcmp(operation,"reset")) {
            if (id>TASKS) return 2;
            cetta_ready_clear(&q); q.capacity=id;
        } else if (!strcmp(operation,"admit")) {
            if (id>=TASKS) return 2;
            status=(int)cetta_ready_admit(&q,&tasks[id].link);
        } else if (!strcmp(operation,"remove")) {
            if (id>=TASKS) return 2;
            status=cetta_ready_remove(&q,&tasks[id].link)?1:0;
        } else if (!strcmp(operation,"visit")) {
            Task *task=(Task *)cetta_ready_rotate(&q);
            if (task) visited=(int)task->id;
        } else return 2;
        validate(&q);
        printf("%d %d",status,visited);
        for (CettaReadyLink *p=q.first;p;p=p->next) printf(" %u",((Task *)p)->id);
        putchar('\n');
    }
    cetta_ready_clear(&q);
    return ferror(stdin)?2:0;
}

static void membership(void) {
    Task a={.id=0},b={.id=1},c={.id=2};
    CettaReadyQueue q={.capacity=2}, other={.capacity=1}, zero={0};
    assert(cetta_ready_rotate(NULL)==NULL);
    assert(cetta_ready_admit(NULL,&a.link)==READY_INVALID);
    assert(cetta_ready_admit(&q,NULL)==READY_INVALID);
    assert(cetta_ready_admit(&zero,&a.link)==READY_FULL);
    assert(cetta_ready_admit(&q,&a.link)==READY_ADMITTED);
    assert(cetta_ready_admit(&q,&b.link)==READY_ADMITTED);
    assert(cetta_ready_admit(&q,&a.link)==READY_PRESENT);
    assert(q.first==&a.link && q.last==&b.link && q.count==2);
    assert(cetta_ready_admit(&q,&c.link)==READY_FULL && !c.link.owner);
    assert(cetta_ready_admit(&other,&a.link)==READY_FOREIGN);
    assert(!cetta_ready_remove(&other,&a.link) && !cetta_ready_remove(&q,&c.link));
    assert(cetta_ready_rotate(&q)==&a.link);
    assert(q.first==&b.link && q.last==&a.link);
    assert(cetta_ready_remove(&q,&a.link));
    assert(!cetta_ready_remove(&q,&a.link));
    assert(cetta_ready_admit(&q,&c.link)==READY_ADMITTED);
    /* The old advancing-array cursor served C here and skipped waiting B. */
    assert(cetta_ready_rotate(&q)==&b.link);
    assert(cetta_ready_remove(&q,&c.link));
    assert(cetta_ready_rotate(&q)==&b.link && q.count==1);
    validate(&q); cetta_ready_clear(&q); validate(&q);
    assert(!a.link.owner && !b.link.owner && !c.link.owner);
    assert(cetta_ready_admit(&other,&a.link)==READY_ADMITTED);
    cetta_ready_clear(&other);
}

/* All completion/yield schedules for a finite cohort. After each completed
 * competitor we admit a new task, so a fixed task must progress amid churn. */
static void service_bound(void) {
    for (unsigned n=1;n<=8;++n) for (unsigned target=0;target<n;++target)
        for (unsigned decisions=0;decisions<(1u<<n);++decisions) {
            Task tasks[TASKS]={0}; CettaReadyQueue q={.capacity=n};
            for (unsigned i=0;i<TASKS;++i) tasks[i].id=i;
            for (unsigned i=0;i<n;++i) assert(cetta_ready_admit(&q,&tasks[i].link)==READY_ADMITTED);
            unsigned fresh=n; bool served=false;
            for (unsigned slot=0;slot<=target;++slot) {
                Task *task=(Task *)cetta_ready_rotate(&q); assert(task);
                if (task->id==target) { served=true; break; }
                if (decisions&(1u<<slot)) {
                    assert(cetta_ready_remove(&q,&task->link));
                    assert(cetta_ready_admit(&q,&tasks[fresh++].link)==READY_ADMITTED);
                }
                validate(&q);
            }
            assert(served);
            cetta_ready_clear(&q);
        }
}

int main(int argc, char **argv) {
    if (argc==2 && !strcmp(argv[1],"--trace")) return trace();
    if (argc!=1) return 2;
    membership(); service_bound();
    puts("ready queue: membership, capacity, retirement and bounded service passed");
    return 0;
}
