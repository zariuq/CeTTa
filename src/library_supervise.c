#include "library_supervise.h"
#include "library.h"
#include "native_handle.h"
#include <stdlib.h>
#include <string.h>

#if CETTA_BUILD_WITH_DURABLE
#include "supervisor.h"
#include "durable_value.h"
#define SUP_KIND "supervisor"
#define PERMIT_KIND "dispatch-permit"
static const char *text(Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING &&
        !memchr(a->ground.sval,0,atom_string_len(a)) ? a->ground.sval : NULL;
}
static bool integer(Atom *a,int64_t *out) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *out=a->ground.ival; return true;
}
static bool boolean(Atom *a,bool *out) {
    return petta_semantics_truth_value(a,out);
}
static Atom *failure(Arena *a,CettaDurableStatus st) {
    return atom_expr(a,(Atom *[]){atom_symbol(a,"supervise:refused"),
        atom_symbol(a,cetta_durable_status_name(st))},2);
}
static Atom *status(Arena *a,CettaDurableStatus st,bool accepted) {
    return st==DURABLE_OK?atom_symbol(a,accepted?"supervise:accepted":"supervise:ignored"):failure(a,st);
}
static void free_supervisor(void *p) { cetta_supervisor_close(p); }
static void free_permit(void *p) { cetta_dispatch_permit_free(p); }
static CettaSupervisor *get_supervisor(CettaLibraryContext *ctx,Atom *arg) {
    uint64_t id;
    return cetta_native_handle_arg(arg,SUP_KIND,&id)?cetta_native_handle_get(ctx,SUP_KIND,id):NULL;
}
static Atom *task_value(Arena *a,const char *epoch,const char *id,CettaSupervisorTask t) {
    CettaDurableStatus st=DURABLE_OK;
    Atom *payload=NULL,*result=atom_symbol(a,"supervise:no-receipt");
    if (st==DURABLE_OK) st=cetta_durable_value_decode(a,t.payload,t.payload_bytes,&payload);
    if (st==DURABLE_OK && t.result_bytes)
        st=cetta_durable_value_decode(a,t.result,t.result_bytes,&result);
    static const char *phases[]={"ready","active","uncertain","succeeded","dead-letter","unresolved"};
    Atom *out=st==DURABLE_OK?atom_expr(a,(Atom *[]){atom_symbol(a,"supervise:task"),
        atom_string(a,epoch),atom_string(a,id),
        atom_int(a,t.generation),atom_int(a,t.remaining),atom_symbol(a,phases[t.phase]),
        atom_bool(a,t.acknowledged),payload,result},9):failure(a,st);
    return out;
}
static Atom *task_view(Arena *a,CettaSupervisor *s,const char *id) {
    CettaSupervisorTask t={0};
    CettaDurableStatus st=cetta_supervisor_get(s,id,&t);
    Atom *out=st==DURABLE_OK?task_value(a,cetta_supervisor_epoch(s),id,t):failure(a,st);
    cetta_supervisor_task_free(&t); return out;
}
static Atom *queue_view(Arena *a,CettaSupervisor *s,bool all_tasks) {
    CettaDurableSnapshot snap={0};
    CettaDurableStatus st=cetta_supervisor_snapshot(s,&snap);
    if (st!=DURABLE_OK) return failure(a,st);
    int64_t counts[7]={0};
    Atom **items=all_tasks?arena_alloc(a,snap.count*sizeof(*items)):NULL;
    for (size_t i=0;st==DURABLE_OK && i<snap.count;++i) {
        CettaSupervisorTask t={0}; st=cetta_supervisor_task_decode(&snap.records[i],&t);
        if (st==DURABLE_OK) {
            ++counts[t.phase]; if (t.acknowledged) ++counts[6];
            if (all_tasks) items[i]=task_value(a,snap.epoch,snap.records[i].key,t);
        }
        cetta_supervisor_task_free(&t);
    }
    Atom *out=NULL;
    if (st!=DURABLE_OK) out=failure(a,st);
    else if (all_tasks) out=atom_expr(a,items,snap.count);
    else {
        Atom *fields[8]={atom_symbol(a,"supervise:queue")};
        for (unsigned i=0;i<7;++i) fields[i+1]=atom_int(a,counts[i]);
        out=atom_expr(a,fields,8);
    }
    cetta_durable_snapshot_free(&snap); return out;
}
#endif

Atom *cetta_supervise_dispatch(CettaLibraryContext *ctx,Space *space,
        Arena *a,Atom *head,Atom **args,uint32_t n) {
    (void)space;
#if CETTA_BUILD_WITH_DURABLE
    const char *stem="__cetta_lib_supervise_";
    if (!head || head->kind!=ATOM_SYMBOL || strncmp(atom_name_cstr(head),stem,strlen(stem))) return NULL;
    const char *name=atom_name_cstr(head)+strlen(stem);
    uint64_t handle;
    if (!strcmp(name,"open") && n==1) {
        const char *path=text(args[0]); CettaSupervisor *s=NULL;
        CettaDurableStatus st=path?cetta_supervisor_open(path,NULL,&s):DURABLE_INVALID;
        if (st!=DURABLE_OK) return failure(a,st);
        if (!cetta_native_handle_alloc(ctx,SUP_KIND,s,free_supervisor,&handle)) {
            cetta_supervisor_close(s); return failure(a,DURABLE_LIMIT);
        }
        return cetta_native_handle_owned_atom(ctx,a,SUP_KIND,handle);
    }
    if ((!strcmp(name,"close") || !strcmp(name,"release")) && n==1) {
        const char *kind=!strcmp(name,"close")?SUP_KIND:PERMIT_KIND;
        bool ok=cetta_native_handle_arg(args[0],kind,&handle) && cetta_native_handle_close(ctx,kind,handle);
        return status(a,DURABLE_OK,ok);
    }
    if (!strcmp(name,"claim") && n==1) {
        CettaDispatchPermit *p=cetta_native_handle_arg(args[0],PERMIT_KIND,&handle)?
            cetta_native_handle_get(ctx,PERMIT_KIND,handle):NULL;
        if (!p) return failure(a,DURABLE_INVALID);
        bool granted=false;
        CettaDurableStatus st=cetta_dispatch_permit_claim(p,&granted);
        if (st!=DURABLE_OK) return failure(a,st);
        if (!granted) return atom_symbol(a,"supervise:no-grant");
        size_t bytes=0; const void *data=cetta_dispatch_permit_payload(p,&bytes); Atom *payload=NULL;
        st=cetta_durable_value_decode(a,data,bytes,&payload);
        if (st!=DURABLE_OK) return failure(a,st);
        return atom_expr(a,(Atom *[]){atom_symbol(a,"supervise:dispatch"),
            atom_string(a,cetta_dispatch_permit_epoch(p)),atom_string(a,cetta_dispatch_permit_id(p)),
            atom_int(a,cetta_dispatch_permit_generation(p)),payload},5);
    }
    CettaSupervisor *s=n?get_supervisor(ctx,args[0]):NULL;
    if (!s) return failure(a,DURABLE_INVALID);
    if (!strcmp(name,"get") && n==2 && text(args[1])) return task_view(a,s,text(args[1]));
    if (!strcmp(name,"tasks") && n==1) return queue_view(a,s,true);
    if (!strcmp(name,"queue") && n==1) return queue_view(a,s,false);
    if (!strcmp(name,"epoch") && n==1) return atom_string(a,cetta_supervisor_epoch(s));
    if (!strcmp(name,"checkpoint") && n==1) return status(a,cetta_supervisor_checkpoint(s),true);
    if (!strcmp(name,"submit") && n==4) {
        int64_t budget; unsigned char *data=NULL; size_t bytes=0;
        if (!text(args[1]) || !integer(args[2],&budget)) return failure(a,DURABLE_INVALID);
        CettaDurableStatus st=cetta_durable_value_encode(args[3],&data,&bytes);
        if (st==DURABLE_OK) st=cetta_supervisor_submit(s,text(args[1]),budget,data,bytes);
        free(data); return status(a,st,true);
    }
    if (!strcmp(name,"next") && n==1) {
        CettaDispatchPermit *p=NULL; CettaDurableStatus st=cetta_supervisor_next(s,&p);
        if (st!=DURABLE_OK) return failure(a,st);
        if (!p) return atom_symbol(a,"supervise:no-ready-task");
        if (!cetta_native_handle_alloc(ctx,PERMIT_KIND,p,free_permit,&handle)) {
            bool ignored;
            cetta_supervisor_uncertain(s,cetta_dispatch_permit_epoch(p),cetta_dispatch_permit_id(p),
                cetta_dispatch_permit_generation(p),&ignored);
            cetta_dispatch_permit_free(p); return failure(a,DURABLE_LIMIT);
        }
        return cetta_native_handle_owned_atom(ctx,a,PERMIT_KIND,handle);
    }
    bool accepted=false;
    if (!strcmp(name,"abandon") && n==2 && text(args[1])) {
        CettaDurableStatus st=cetta_supervisor_abandon(s,text(args[1]),&accepted);
        return status(a,st,accepted);
    }
    if ((!strcmp(name,"uncertain") || !strcmp(name,"acknowledge")) && n==4) {
        int64_t generation;
        if (!text(args[1]) || !text(args[2]) || !integer(args[3],&generation)) return failure(a,DURABLE_INVALID);
        CettaDurableStatus st=!strcmp(name,"uncertain")?
            cetta_supervisor_uncertain(s,text(args[1]),text(args[2]),generation,&accepted):
            cetta_supervisor_acknowledge(s,text(args[1]),text(args[2]),generation,&accepted);
        return status(a,st,accepted);
    }
    if (!strcmp(name,"reply") && n==8) {
        int64_t generation; bool succeeded,retry;
        if (!text(args[1]) || !text(args[2]) || !integer(args[3],&generation) ||
            !boolean(args[5],&succeeded) || !boolean(args[6],&retry)) return failure(a,DURABLE_INVALID);
        CettaSupervisorReplyKind kind;
        if (atom_is_symbol(args[4],"complete")) kind=SUP_COMPLETE;
        else if (atom_is_symbol(args[4],"reconcile")) kind=SUP_RECONCILE;
        else return failure(a,DURABLE_INVALID);
        unsigned char *data=NULL; size_t bytes=0;
        CettaDurableStatus st=cetta_durable_value_encode(args[7],&data,&bytes);
        if (st==DURABLE_OK) st=cetta_supervisor_reply(s,text(args[1]),text(args[2]),generation,
            kind,succeeded,retry,data,bytes,&accepted);
        else { /* The effect may have run even when its receipt cannot be serialized. */
            bool ignored; cetta_supervisor_uncertain(s,text(args[1]),text(args[2]),generation,&ignored);
        }
        free(data); return status(a,st,accepted);
    }
    return failure(a,DURABLE_INVALID);
#else
    (void)ctx; (void)a; (void)head; (void)args; (void)n;
    return NULL;
#endif
}
