#define _POSIX_C_SOURCE 200809L
#include "supervisor.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *kill_at;
void cetta_durable_test_boundary(const char *boundary) {
    if (kill_at && !strcmp(kill_at,boundary)) _exit(86);
}
static void ok(CettaDurableStatus st) { if (st!=DURABLE_OK) fprintf(stderr,"unexpected durable status: %s\n",cetta_durable_status_name(st)); assert(st==DURABLE_OK); }
static void task(CettaSupervisor *s,const char *id,CettaSupervisorPhase phase,
        int64_t remaining,int64_t generation,bool ack) {
    CettaSupervisorTask t={0}; ok(cetta_supervisor_get(s,id,&t));
    assert(t.phase==phase && t.remaining==remaining && t.generation==generation && t.acknowledged==ack);
    cetta_supervisor_task_free(&t);
}
static void reply(CettaSupervisor *s,const char *id,int64_t gen,CettaSupervisorReplyKind kind,
        bool success,bool retry,bool expected) {
    bool accepted=false;
    ok(cetta_supervisor_reply(s,cetta_supervisor_epoch(s),id,gen,kind,success,retry,"receipt",7,&accepted));
    assert(accepted==expected);
}
static bool claim(CettaDispatchPermit *p) { bool granted=false; ok(cetta_dispatch_permit_claim(p,&granted)); return granted; }
typedef struct { CettaDispatchPermit *permit; int granted; } Claim;
static void *claim_worker(void *arg) {
    Claim *c=arg; c->granted=claim(c->permit); return NULL;
}
static void lifecycle(const char *path) {
    CettaSupervisor *s=NULL; ok(cetta_supervisor_open(path,NULL,&s));
    assert(cetta_supervisor_submit(s,"zero",0,"p",1)==DURABLE_INVALID);
    ok(cetta_supervisor_submit(s,"poison",3,"p",1));
    ok(cetta_supervisor_submit(s,"healthy",1,"h",1));
    assert(cetta_supervisor_submit(s,"poison",99,"reset",5)==DURABLE_PRECONDITION);
    CettaDispatchPermit *p=NULL; ok(cetta_supervisor_next(s,&p));
    assert(p && !strcmp(cetta_dispatch_permit_id(p),"poison"));
    task(s,"poison",SUP_ACTIVE,2,1,false);
    bool accepted=true;
    ok(cetta_supervisor_acknowledge(s,cetta_supervisor_epoch(s),"poison",1,&accepted)); assert(!accepted);
    Claim claims[16]; pthread_t threads[16]; int count=0;
    for (int i=0;i<16;++i) { claims[i]=(Claim){p,0}; assert(!pthread_create(&threads[i],NULL,claim_worker,&claims[i])); }
    for (int i=0;i<16;++i) { assert(!pthread_join(threads[i],NULL)); count+=claims[i].granted; }
    assert(count==1); cetta_dispatch_permit_free(p);
    reply(s,"poison",1,SUP_COMPLETE,false,true,true);
    ok(cetta_supervisor_next(s,&p)); assert(!strcmp(cetta_dispatch_permit_id(p),"healthy"));
    assert(claim(p)); cetta_dispatch_permit_free(p);
    reply(s,"healthy",1,SUP_COMPLETE,true,false,true);
    ok(cetta_supervisor_next(s,&p)); assert(!strcmp(cetta_dispatch_permit_id(p),"poison"));
    assert(claim(p)); cetta_dispatch_permit_free(p);
    reply(s,"poison",1,SUP_COMPLETE,true,false,false); /* stale generation */
    ok(cetta_supervisor_reply(s,"00000000000000000000000000000000","poison",2,
        SUP_COMPLETE,true,false,"late",4,&accepted)); assert(!accepted);
    reply(s,"missing",2,SUP_COMPLETE,true,false,false);
    reply(s,"poison",2,SUP_COMPLETE,false,true,true);
    ok(cetta_supervisor_next(s,&p)); assert(cetta_dispatch_permit_generation(p)==3);
    assert(claim(p)); cetta_dispatch_permit_free(p);
    reply(s,"poison",3,SUP_COMPLETE,false,true,true);
    task(s,"poison",SUP_DEAD_LETTER,0,3,false);
    reply(s,"poison",3,SUP_COMPLETE,true,false,false); /* terminal stable */
    ok(cetta_supervisor_acknowledge(s,cetta_supervisor_epoch(s),"poison",3,&accepted)); assert(accepted);
    ok(cetta_supervisor_next(s,&p)); assert(!p);
    ok(cetta_supervisor_checkpoint(s)); cetta_supervisor_close(s);
    ok(cetta_supervisor_open(path,NULL,&s)); task(s,"poison",SUP_DEAD_LETTER,0,3,true);
    assert(cetta_supervisor_submit(s,"poison",3,"reset",5)==DURABLE_PRECONDITION);
    ok(cetta_supervisor_submit(s,"uncertain",2,"u",1));
    ok(cetta_supervisor_next(s,&p)); assert(claim(p));
    cetta_supervisor_close(s); assert(!claim(p));
    cetta_dispatch_permit_free(p);
    ok(cetta_supervisor_open(path,NULL,&s)); task(s,"uncertain",SUP_UNCERTAIN,1,1,false);
    reply(s,"uncertain",1,SUP_COMPLETE,true,false,false);
    ok(cetta_supervisor_submit(s,"independent",1,"i",1));
    ok(cetta_supervisor_next(s,&p)); assert(!strcmp(cetta_dispatch_permit_id(p),"independent"));
    cetta_dispatch_permit_free(p);
    reply(s,"uncertain",1,SUP_RECONCILE,false,true,true);
    ok(cetta_supervisor_next(s,&p)); assert(!strcmp(cetta_dispatch_permit_id(p),"uncertain"));
    assert(cetta_dispatch_permit_generation(p)==2); cetta_dispatch_permit_free(p);
    ok(cetta_supervisor_uncertain(s,cetta_supervisor_epoch(s),"uncertain",2,&accepted)); assert(accepted);
    ok(cetta_supervisor_abandon(s,"uncertain",&accepted)); assert(accepted);
    task(s,"uncertain",SUP_UNRESOLVED,0,2,false);
    cetta_supervisor_close(s);
}
static void crash(const char *path,const char *boundary,bool committed) {
    CettaSupervisor *s=NULL; ok(cetta_supervisor_open(path,NULL,&s));
    ok(cetta_supervisor_submit(s,"crash",3,"payload",7)); cetta_supervisor_close(s);
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        ok(cetta_supervisor_open(path,NULL,&s)); kill_at=boundary;
        CettaDispatchPermit *p=NULL; (void)cetta_supervisor_next(s,&p); _exit(87);
    }
    int status=0; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==86);
    ok(cetta_supervisor_open(path,NULL,&s));
    task(s,"crash",committed?SUP_UNCERTAIN:SUP_READY,committed?2:3,committed?1:0,false);
    cetta_supervisor_close(s);
}
int main(int argc,char **argv) {
    assert(argc==2); char path[4096];
    assert(snprintf(path,sizeof(path),"%s/lifecycle.db",argv[1])<(int)sizeof(path)); lifecycle(path);
    const char *points[]={"operation","before_commit","after_commit"};
    for (unsigned i=0;i<3;++i) {
        assert(snprintf(path,sizeof(path),"%s/crash-%u.db",argv[1],i)<(int)sizeof(path));
        crash(path,points[i],i==2);
    }
    puts("PASS durable permits, restart, correlation, queue, retirement and crash boundaries");
    return 0;
}
