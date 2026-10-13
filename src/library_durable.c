#include "library_durable.h"
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_BUILD_WITH_DURABLE
#define CETTA_BUILD_WITH_DURABLE 0
#endif

static Atom *result(Arena *a, const char *head, const char *detail) {
    Atom *items[]={atom_symbol(a,head),detail?atom_symbol(a,detail):NULL};
    return atom_expr(a,items,detail?2:1);
}

#if CETTA_BUILD_WITH_DURABLE
#include "durable_store.h"
#include "durable_value.h"
#include <pthread.h>

struct CettaDurableRuntime { pthread_mutex_t mutex; CettaDurableStore *store; };

CettaDurableRuntime *cetta_durable_runtime_new(void) {
    CettaDurableRuntime *r=calloc(1,sizeof(*r));
    if (r && pthread_mutex_init(&r->mutex,NULL)) { free(r); return NULL; }
    return r;
}

void cetta_durable_runtime_free(CettaDurableRuntime *r) {
    if (r) { cetta_durable_close(r->store); pthread_mutex_destroy(&r->mutex); free(r); }
}

static const char *text(Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING ? a->ground.sval : NULL;
}

static Atom *failure(Arena *a, CettaDurableStatus s) {
    return result(a,"durable:failure",cetta_durable_status_name(s));
}

static Atom *view(Arena *a, CettaDurableSnapshot *v) {
    Atom **records=arena_alloc(a,v->count*sizeof(*records));
    for (size_t i=0;i<v->count;++i) {
        CettaDurableRecord *r=&v->records[i]; Atom *value=NULL;
        CettaDurableStatus s=cetta_durable_value_decode(a,r->data,r->size,&value);
        if (s!=DURABLE_OK) {
            Atom *error[]={atom_symbol(a,"durable:failure"),atom_symbol(a,cetta_durable_status_name(s)),
                atom_string(a,r->space),atom_string(a,r->key)};
            return atom_expr(a,error,4);
        }
        Atom *items[]={atom_symbol(a,"durable:record"),atom_string(a,r->space),
            atom_string(a,r->key),value};
        records[i]=atom_expr(a,items,4);
    }
    Atom *items[]={atom_symbol(a,"durable:snapshot"),atom_string(a,v->epoch),
        atom_int(a,v->revision),atom_expr(a,records,v->count)};
    return atom_expr(a,items,4);
}

static Atom *commit(CettaDurableRuntime *r, Arena *a, Atom **args) {
    const char *epoch=text(args[0]); Atom *rev=args[1], *batch=args[2];
    if (!epoch || rev->kind!=ATOM_GROUNDED || rev->ground.gkind!=GV_INT ||
        batch->kind!=ATOM_EXPR) return failure(a,DURABLE_INVALID);
    CettaExprLen n=batch->expr.len;
    if (n>cetta_durable_default_limits().operations) return failure(a,DURABLE_LIMIT);
    if (!n) return failure(a,DURABLE_INVALID);
    CettaDurableOp *ops=calloc(n,sizeof(*ops));
    if (!ops) return failure(a,DURABLE_NOMEM);
    CettaDurableStatus s=DURABLE_OK;
    size_t total=0;
    for (CettaExprLen i=0;i<n;++i) {
        Atom *op=batch->expr.elems[i];
        if (op->kind!=ATOM_EXPR || op->expr.len<3) { s=DURABLE_INVALID; break; }
        Atom **e=op->expr.elems;
        if (atom_is_symbol(e[0],"durable:insert")) ops[i].kind=DURABLE_INSERT;
        else if (atom_is_symbol(e[0],"durable:replace")) ops[i].kind=DURABLE_REPLACE;
        else if (atom_is_symbol(e[0],"durable:remove")) ops[i].kind=DURABLE_REMOVE;
        else { s=DURABLE_INVALID; break; }
        if (op->expr.len!=(ops[i].kind==DURABLE_REMOVE?3:4) ||
            !(ops[i].space=text(e[1])) || !(ops[i].key=text(e[2]))) { s=DURABLE_INVALID; break; }
        if (ops[i].kind!=DURABLE_REMOVE) {
            unsigned char *data=NULL;
            s=cetta_durable_value_encode(e[3],&data,&ops[i].size); ops[i].data=data;
            if (s!=DURABLE_OK) break;
        }
        size_t bytes=ops[i].size+strlen(ops[i].space)+strlen(ops[i].key);
        if (bytes>cetta_durable_default_limits().batch_bytes-total) { s=DURABLE_LIMIT; break; }
        total+=bytes;
    }
    int64_t published=-1;
    if (s==DURABLE_OK) s=cetta_durable_commit(r->store,epoch,rev->ground.ival,ops,n,&published);
    for (CettaExprLen i=0;i<n;++i) free((void *)ops[i].data);
    free(ops);
    if (s!=DURABLE_OK) return failure(a,s);
    Atom *items[]={atom_symbol(a,"durable:committed"),atom_string(a,epoch),atom_int(a,published)};
    return atom_expr(a,items,3);
}

static Atom *dispatch(CettaDurableRuntime *r, Arena *a, const char *name, Atom **args, uint32_t n) {
    if (!strcmp(name,"open") && n==1) {
        const char *path=text(args[0]);
        if (!path) return failure(a,DURABLE_INVALID);
        if (r->store) return failure(a,DURABLE_BUSY);
        CettaDurableStatus s=cetta_durable_open(path,NULL,&r->store);
        if (s!=DURABLE_OK) return failure(a,s);
        CettaDurableSnapshot v;
        s=cetta_durable_recover(r->store,&v);
        if (s!=DURABLE_OK) { cetta_durable_close(r->store); r->store=NULL; return failure(a,s); }
        Atom *answer=view(a,&v); cetta_durable_snapshot_free(&v); return answer;
    }
    if (!r->store) return result(a,"durable:failure","closed");
    if (!strcmp(name,"read") && n==1) {
        const char *space=text(args[0]);
        if (!space) return failure(a,DURABLE_INVALID);
        CettaDurableSnapshot v;
        CettaDurableStatus s=cetta_durable_snapshot(r->store,!strcmp(space,"*")?NULL:space,&v);
        if (s!=DURABLE_OK) return failure(a,s);
        Atom *answer=view(a,&v); cetta_durable_snapshot_free(&v); return answer;
    }
    if (!strcmp(name,"commit") && n==3) return commit(r,a,args);
    bool zero=n==0 || (n==1 && args[0]->kind==ATOM_EXPR && args[0]->expr.len==0);
    if (!strcmp(name,"checkpoint") && zero) {
        CettaDurableStatus s=cetta_durable_checkpoint(r->store);
        return s==DURABLE_OK ? result(a,"durable:checkpointed",NULL) : failure(a,s);
    }
    if (!strcmp(name,"close") && zero) {
        cetta_durable_close(r->store); r->store=NULL;
        return result(a,"durable:closed",NULL);
    }
    return failure(a,DURABLE_INVALID);
}
#else
CettaDurableRuntime *cetta_durable_runtime_new(void) { return NULL; }
void cetta_durable_runtime_free(CettaDurableRuntime *r) { (void)r; }
#endif

Atom *cetta_durable_admin_dispatch(CettaDurableRuntime *r, Arena *a, Atom *head,
        Atom **args, uint32_t n) {
    if (!head || head->kind!=ATOM_SYMBOL) return NULL;
    const char *name=atom_name_cstr(head), *prefix="__cetta_lib_durable_";
    if (strncmp(name,prefix,strlen(prefix))) return NULL;
#if CETTA_BUILD_WITH_DURABLE
    if (!r) return result(a,"durable:failure","allocation");
    pthread_mutex_lock(&r->mutex);
    Atom *answer=dispatch(r,a,name+strlen(prefix),args,n);
    pthread_mutex_unlock(&r->mutex);
    return answer;
#else
    (void)r; (void)args; (void)n;
    return result(a,"durable:failure","unavailable");
#endif
}

/* Ordinary reduction, including hidden calls through user equations, never
 * owns commit authority. A selected transition uses the store API; the CLI
 * handles explicit administrative directives outside the evaluator. */
Atom *cetta_durable_dispatch(CettaDurableRuntime *r, Arena *a, Atom *head,
        Atom **args, uint32_t n) {
    (void)r; (void)args; (void)n;
    if (!head || head->kind!=ATOM_SYMBOL ||
        strncmp(atom_name_cstr(head),"__cetta_lib_durable_",20)) return NULL;
    return result(a,"durable:failure","commit-boundary-required");
}
