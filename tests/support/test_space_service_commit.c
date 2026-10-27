/* Exercise the storage owner's real publication and recovery boundary. Faults
 * are injected at allocation and I/O interfaces, without production switches. */
#define _GNU_SOURCE
#include <time.h>
static int scope_clock(clockid_t,struct timespec *);
#define clock_gettime scope_clock
#define main cetta_space_service_entry
#include "../../src/space_service_main.c"
#undef main
#undef clock_gettime

static uint64_t fake_clock;
static int scope_clock(clockid_t clock,struct timespec *value) {
    if (!fake_clock || clock!=CLOCK_MONOTONIC) return clock_gettime(clock,value);
    uint64_t now=fake_clock++;
    *value=(struct timespec){.tv_sec=(time_t)(now/1000),.tv_nsec=(long)(now%1000)*1000000L};
    return 0;
}

static unsigned checks, after_write_allocations, event_encodings;
static int observed_journal=-1;
static bool write_started, forbid_after_write, fail_sync, fail_projection, fail_event, fail_key;
static size_t fail_copy_size;
static unsigned write_fault, allowed_projections;
static unsigned scope_row_encodings;

#define check(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr,"FAIL at line %d: %s\n",__LINE__,#condition); \
        abort(); \
    } \
} while (0)

void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
static bool refused_allocation(void) {
    if (write_started) ++after_write_allocations;
    if (write_started && forbid_after_write) { errno=ENOMEM; return true; }
    return false;
}
void *__wrap_malloc(size_t size) {
    if (refused_allocation()) return NULL;
    if (fail_copy_size && size==fail_copy_size) {
        fail_copy_size=0; errno=ENOMEM; return NULL;
    }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count,size_t size) {
    return refused_allocation() ? NULL : __real_calloc(count,size);
}
void *__wrap_realloc(void *pointer,size_t size) {
    return refused_allocation() ? NULL : __real_realloc(pointer,size);
}
ssize_t __real_write(int,const void *,size_t);
ssize_t __wrap_write(int fd,const void *data,size_t size) {
    if (fd==observed_journal) {
        write_started=true;
        if (write_fault==1) { write_fault=0; errno=EIO; return -1; }
        if (write_fault==2) {
            write_fault=1;
            return __real_write(fd,data,size>5 ? 5 : size);
        }
    }
    return __real_write(fd,data,size);
}
int __real_fsync(int);
int __wrap_fsync(int fd) {
    if (fd==observed_journal && fail_sync) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
CettaAtomWireStatus __real_cetta_atom_wire_encode(const Atom *,unsigned char **,size_t *);
CettaAtomWireStatus __wrap_cetta_atom_wire_encode(const Atom *atom,unsigned char **data,size_t *size) {
    if (shape((Atom *)atom,"ScopeProbe",3)) ++scope_row_encodings;
    if (write_started && fail_key &&
        (atom_is_symbol((Atom *)atom,"saved") || shape((Atom *)atom,"saved-open",2))) {
        fail_key=false; *data=NULL; *size=0; return ATOM_WIRE_NOMEM;
    }
    if (shape((Atom *)atom,"event-bytes",3) || shape((Atom *)atom,"event-from",4) ||
        shape((Atom *)atom,"event-authorized",8) || shape((Atom *)atom,"event-authorized-v2",9) ||
        shape((Atom *)atom,"event-authorized-v3",10) ||
        shape((Atom *)atom,"event-authorized-v4",12) ||
        shape((Atom *)atom,"event-authorized-v5",15) ||
        shape((Atom *)atom,"event-authorized-observed-v1",16)) {
        ++event_encodings;
        if (fail_event) { *data=NULL; *size=0; return ATOM_WIRE_NOMEM; }
    }
    return __real_cetta_atom_wire_encode(atom,data,size);
}
bool __real_space_admit_atom(Space *,Arena *,Atom *);
bool __wrap_space_admit_atom(Space *space,Arena *arena,Atom *atom) {
    if (write_started && fail_projection) {
        if (!allowed_projections) return false;
        --allowed_projections;
    }
    return __real_space_admit_atom(space,arena,atom);
}

static void reset_faults(void) {
    observed_journal=-1; write_started=false; forbid_after_write=false;
    fail_sync=fail_projection=fail_event=fail_key=false;
    fail_copy_size=0; write_fault=allowed_projections=0;
    after_write_allocations=event_encodings=0;
    scope_row_encodings=0;
}
static void release(Service *s) {
    reset_faults();
    for (size_t i=0;i<STORES;++i) { store_free(s->stores[i]); s->stores[i]=NULL; }
    for (size_t i=0;i<SNAPSHOTS;++i) {
        snapshot_free(s,&s->snapshots[i]); observation_free(s,&s->observations[i]);
    }
    for (size_t i=0;i<s->receipt_count;++i) receipt_free(&s->receipts[i]);
    free(s->decisions); free(s->decision_slots);
    free(s->principals); free(s->principal_slots);
    s->decisions=NULL; s->decision_slots=NULL;
    s->principals=NULL; s->principal_slots=NULL;
    s->decision_count=s->decision_capacity=s->decision_slot_capacity=0;
    s->principal_count=s->principal_capacity=s->principal_slot_capacity=0;
    close(s->journal); close(s->lock); close(s->directory);
}
static void initialize(Service *s,const char *directory) {
    *s=(Service){.journal=-1,.directory=-1,.lock=-1,
        .max_facts=1000000,.max_live_bytes=64*1024*1024,
        .max_query_bytes=QUERY_BYTES,.max_snapshot_bytes=SNAPSHOT_BYTES,
        .query_ms=5000,.max_watchers=CLIENTS/2,.next_cursor=1,
        .process_uid=(uint32_t)geteuid(),.owner_uid=(uint32_t)geteuid()};
    check(directory_open(s,directory)); check(recover(s));
    check(random_id(s->boot));
}
static off_t journal_size(Service *s) {
    struct stat info; check(!fstat(s->journal,&info)); return info.st_size;
}
static Atom *request(Arena *a,Service *s,const char *id,const char *operation,Atom **args,size_t count) {
    Atom *parts[10]={atom_string(a,id),atom_symbol(a,operation),atom_string(a,s->uuid)};
    check(count<=7);
    for (size_t i=0;i<count;++i) parts[i+3]=args[i];
    return form(a,"request",parts,count+3);
}
static Principal root_principal(Service *s) {
    return (Principal){.uid=s->process_uid};
}
static Principal test_principal(Service *s,uint32_t uid) {
    Principal principal=current_principal(s,uid);
    if (!principal.epoch) principal=legacy_principal(s,uid);
    check(principal.epoch); return principal;
}
static Client test_client(Service *s,uint32_t uid) {
    return (Client){.fd=-1,.principal=test_principal(s,uid)};
}
static Atom *execute(Service *s,Arena *a,Atom *request,Bytes raw) {
    Client client={.fd=-1,.principal=root_principal(s),.owner=true};
    return dispatch(s,&client,a,request,raw);
}
static Atom *open_request(Arena *a,Service *s,const char *backend) {
    Atom *args[]={atom_string(a,backend),atom_symbol(a,backend),atom_int(a,s->catalog_revision)};
    return request(a,s,"open-store","open",args,3);
}
static Atom *batch_request(Arena *a,Service *s,const char *id,bool add) {
    Atom *row=atom_symbol(a,"saved");
    Atom *add_args[]={row}; Atom *operation=form(a,"add",add_args,1);
    Atom *ops=form(a,"ops",&operation,add ? 1 : 0);
    Atom *args[]={atom_int(a,s->stores[0]->id),atom_int(a,s->stores[0]->revision),ops};
    return request(a,s,id,"batch",args,3);
}
static Atom *execute_wire(Service *s,Arena *a,Atom *request) {
    Bytes raw={0}; check(wire(request,&raw));
    Atom *answer=execute(s,a,request,raw); bytes_free(&raw); return answer;
}
static Atom *execute_client_wire(Service *s,Arena *a,Client *client,Atom *request) {
    Bytes raw={0}; check(wire(request,&raw));
    Atom *answer=dispatch(s,client,a,request,raw); bytes_free(&raw); return answer;
}
static Atom *execute_client_without_postwrite_allocation(Service *s,Arena *a,Client *client,
                                                         Atom *request) {
    Bytes raw={0}; check(wire(request,&raw));
    reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=dispatch(s,client,a,request,raw);
    check(write_started && !after_write_allocations && !s->poisoned);
    reset_faults(); bytes_free(&raw); return answer;
}
static void success(Atom *answer) { check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"ok")); }
static void failure(Atom *answer,const char *reason) {
    check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"error") &&
          atom_is_symbol(part(answer,3),reason));
}

static void publication(Service *s,Arena *a,Atom *req) {
    Bytes raw={0}; check(wire(req,&raw));
    reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=execute(s,a,req,raw);
    check(write_started); check(!s->poisoned); success(answer);
    check(!after_write_allocations); check(event_encodings==1);
    reset_faults();
    const char *id=NULL; check(token(part(req,1),&id));
    Receipt *receipt=find_receipt(s,root_principal(s),id);
    check(receipt && same_bytes(receipt->request,raw));
    Bytes response={0}; check(wire(answer,&response)); check(same_bytes(receipt->reply,response));
    success(execute(s,a,req,raw));
    bytes_free(&raw); bytes_free(&response);
}
static void prepared_publication(Service *s,Arena *a,const char *backend) {
    publication(s,a,open_request(a,s,backend));
    check(s->next_store==2 && s->catalog_revision==1);
    publication(s,a,batch_request(a,s,"no-allocation-noop",false));
    check(s->stores[0]->revision==1 && !space_length64(s->stores[0]->space));
    Atom *args[]={atom_int(a,s->stores[0]->id),atom_int(a,1),atom_int(a,s->catalog_revision)};
    publication(s,a,request(a,s,"no-allocation-drop","drop",args,3));
    check(!s->stores[0] && s->catalog_revision==2);
}
static void preparation_refusal(Service *s,Arena *a,bool packet_failure) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    /* Fill the real bounded retry cache: failed preparation must not evict it. */
    for (unsigned i=0;i<RECEIPTS;++i) {
        char id[40]; snprintf(id,sizeof(id),"retained-%u",i);
        success(execute_wire(s,a,batch_request(a,s,id,false)));
    }
    check(s->receipt_count==RECEIPTS);
    Bytes oldest={0}; check(bytes_copy(&oldest,s->receipts[0].reply.data,s->receipts[0].reply.size));
    uint64_t revision=s->stores[0]->revision, written=s->journal_bytes;
    size_t retained_bytes=s->receipt_bytes;
    size_t retained_decisions=s->decision_count;
    off_t before=journal_size(s);
    Atom *req=batch_request(a,s,"refused-preparation",true); Bytes raw={0}; check(wire(req,&raw));
    reset_faults(); observed_journal=s->journal;
    if (packet_failure) fail_event=true;
    else fail_copy_size=raw.size;
    Atom *answer=execute(s,a,req,raw);
    check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"error"));
    check(atom_is_symbol(part(answer,3),packet_failure ? "limit" : "allocation"));
    check(!write_started && !s->poisoned && !fail_copy_size);
    reset_faults();
    check(journal_size(s)==before && s->journal_bytes==written);
    check(s->stores[0]->revision==revision && !space_length64(s->stores[0]->space));
    check(s->receipt_count==RECEIPTS && s->receipt_bytes==retained_bytes);
    check(s->decision_count==retained_decisions);
    Receipt *kept=find_receipt(s,root_principal(s),"retained-0");
    check(kept && same_bytes(kept->reply,oldest));
    check(!find_receipt(s,root_principal(s),"refused-preparation"));
    check(!find_decision(s,root_principal(s),"refused-preparation"));
    success(execute(s,a,req,raw));
    check(s->stores[0]->revision==revision+1 && space_length64(s->stores[0]->space)==1);
    check(!find_receipt(s,root_principal(s),"retained-0") &&
          find_receipt(s,root_principal(s),"refused-preparation"));
    check(s->decision_count==retained_decisions+1 &&
          find_decision(s,root_principal(s),"refused-preparation"));
    bytes_free(&raw); bytes_free(&oldest);
}
static void uncertain_publication(Service *s,Arena *a,const char *directory,unsigned fault) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    Atom *req=batch_request(a,s,"uncertain",true);
    if (fault==5 || fault==6) {
        Atom *row=fault==5 ? atom_symbol(a,"saved")
                          : form(a,"saved-open",(Atom *[]){atom_var(a,"x")},1);
        Atom *add=form(a,"add",&row,1);
        Atom *args[]={atom_int(a,s->stores[0]->id),atom_int(a,0),form(a,"ops",&add,1)};
        success(execute_wire(s,a,request(a,s,"seed","batch",args,3)));
        Atom *remove=form(a,"remove",&row,1);
        args[1]=atom_int(a,1); args[2]=form(a,"ops",&remove,1);
        req=request(a,s,"uncertain","batch",args,3);
    }
    if (fault==7) {
        Atom *row=atom_symbol(a,"saved"), *add=form(a,"add",&row,1);
        Atom *both[]={add,add};
        Atom *args[]={atom_int(a,s->stores[0]->id),atom_int(a,0),form(a,"ops",both,2)};
        req=request(a,s,"uncertain","batch",args,3);
    }
    uint64_t initial_revision=s->stores[0]->revision;
    Bytes raw={0}; check(wire(req,&raw));
    off_t before=journal_size(s);
    reset_faults(); observed_journal=s->journal;
    if (fault<=2) write_fault=fault;
    else if (fault==3) fail_sync=true;
    else if (fault==4) fail_projection=true;
    else if (fault<=6) fail_key=true;
    else { fail_projection=true; allowed_projections=1; }
    check(!execute(s,a,req,raw)); check(s->poisoned && write_started);
    check(!find_receipt(s,root_principal(s),"uncertain")); check(!fail_key);
    if (fault==7) check(space_length64(s->stores[0]->space)==1 && !s->stores[0]->revision);
    check(!execute(s,a,req,raw));
    reset_faults(); release(s); initialize(s,directory);
    bool complete=fault>=3;
    uint64_t final_count=fault==7 ? 2u : fault>=5 ? 0u : 1u;
    check(space_length64(s->stores[0]->space)==(complete ? final_count : 0u));
    check(s->stores[0]->revision==initial_revision+(complete ? 1u : 0u));
    check((find_receipt(s,root_principal(s),"uncertain")!=NULL)==complete);
    if (!complete) check(journal_size(s)==before);
    success(execute(s,a,req,raw));
    check(space_length64(s->stores[0]->space)==final_count &&
          s->stores[0]->revision==initial_revision+1);
    bytes_free(&raw);
}
static void account_publication(Service *s,Arena *a,const char *directory) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    uint32_t member_uid=s->process_uid+1;
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,member_uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),account,rights_atom(a,RIGHT_WRITE),
                  atom_symbol(a,"none"),atom_symbol(a,"none")};
    success(execute_wire(s,a,request(a,s,"grant-member","grant",args,5)));
    /* The two accounts may reuse an ID without sharing a receipt. The member's
     * empty batch also has no allocation after its journal write begins. */
    Atom *req=batch_request(a,s,"open-store",false);
    Bytes raw={0}; check(wire(req,&raw));
    Client member=test_client(s,member_uid);
    reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=dispatch(s,&member,a,req,raw);
    check(write_started && !s->poisoned); success(answer);
    check(!after_write_allocations && event_encodings==1);
    reset_faults();
    check(find_receipt(s,root_principal(s),"open-store")!=NULL);
    check(find_receipt(s,member.principal,"open-store")!=NULL);
    Bytes response={0}; check(wire(answer,&response));
    release(s); initialize(s,directory);
    member=test_client(s,member_uid);
    Receipt *receipt=find_receipt(s,member.principal,"open-store");
    check(receipt && same_bytes(receipt->reply,response));
    check(find_receipt(s,root_principal(s),"open-store")!=NULL);
    success(dispatch(s,&member,a,req,raw));
    check(s->stores[0]->revision==1);
    bytes_free(&raw); bytes_free(&response);
}
static Atom *grant_request(Arena *a,Service *s,const char *request_id,uint32_t uid,
                           unsigned rights) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),account,rights_atom(a,rights),
                  atom_symbol(a,"none"),atom_symbol(a,"none")};
    return request(a,s,request_id,"grant",args,5);
}
static Atom *grant_ticketed_request(Arena *a,Service *s,const char *request_id,
                                    uint32_t uid,unsigned rights,unsigned ticket_rights) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),account,rights_atom(a,rights),
                  rights_atom(a,ticket_rights),atom_symbol(a,"none"),atom_symbol(a,"none")};
    return request(a,s,request_id,"grant-ticketed",args,6);
}
static Atom *fetch_request(Arena *a,Service *s,const char *request_id,uint64_t cursor) {
    Atom *parts[]={atom_string(a,request_id),atom_symbol(a,"fetch"),atom_string(a,s->boot),
                   atom_int(a,cursor),atom_int(a,0),atom_int(a,1)};
    return form(a,"request",parts,6);
}
static Atom *issue_ticket_request(Arena *a,Service *s,const char *request_id,
                                  uint32_t uid,unsigned right) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),account,rights_atom(a,right)};
    return request(a,s,request_id,"issue-ticket",args,3);
}
static Atom *batch_ticket_request(Arena *a,Service *s,const char *request_id,
                                  uint64_t ticket) {
    Atom *args[]={atom_int(a,s->stores[0]->id),atom_int(a,s->stores[0]->revision),
                  atom_int(a,ticket),form(a,"ops",NULL,0)};
    return request(a,s,request_id,"batch-ticket",args,4);
}
static Atom *grant_root_request(Arena *a,Service *s,const char *request_id,
                                uint32_t uid,unsigned rights,unsigned ticket_rights,
                                bool delegable) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),account,rights_atom(a,rights),
        rights_atom(a,ticket_rights),atom_symbol(a,delegable ? "true" : "false"),
        atom_symbol(a,"none"),atom_symbol(a,"none")};
    return request(a,s,request_id,"grant-root",args,7);
}
static Atom *delegate_grant_request(Arena *a,Service *s,const char *request_id,
                                    Grant *parent,uint32_t uid,unsigned rights,
                                    unsigned ticket_rights,bool delegable) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={atom_int(a,s->stores[0]->id),
        grant_ref_atom(s,a,s->stores[0],parent),account,rights_atom(a,rights),
        rights_atom(a,ticket_rights),atom_symbol(a,delegable ? "true" : "false")};
    return request(a,s,request_id,"delegate-grant",args,6);
}
static Atom *issue_ticket_v2_request(Arena *a,Service *s,const char *request_id,
                                     Grant *grant,unsigned right) {
    Atom *args[]={atom_int(a,s->stores[0]->id),
        grant_ref_atom(s,a,s->stores[0],grant),rights_atom(a,right)};
    return request(a,s,request_id,"issue-ticket-v2",args,3);
}
static Atom *append_authorized_request(Arena *a,Service *s,const char *request_id,
                                       Grant *grant,Ticket *ticket,const char *value) {
    Atom *bundle=form(a,"bundle",(Atom *[]){atom_symbol(a,value)},1);
    Atom *ticketRef=ticket ? ticket_ref_atom(s,a,s->stores[0],ticket)
                           : atom_symbol(a,"none");
    Atom *args[]={atom_int(a,s->stores[0]->id),
        grant_ref_atom(s,a,s->stores[0],grant),ticketRef,bundle};
    return request(a,s,request_id,"append-authorized",args,4);
}
static Atom *append_authorized_bundle_request(Arena *a,Service *s,const char *request_id,
                                              Grant *grant,Ticket *ticket,Atom *bundle) {
    Atom *ticketRef=ticket ? ticket_ref_atom(s,a,s->stores[0],ticket)
                           : atom_symbol(a,"none");
    Atom *args[]={atom_int(a,s->stores[0]->id),
        grant_ref_atom(s,a,s->stores[0],grant),ticketRef,bundle};
    return request(a,s,request_id,"append-authorized",args,4);
}
static Atom *revoke_grant_request(Arena *a,Service *s,const char *request_id,Grant *grant) {
    Atom *args[]={atom_int(a,s->stores[0]->id),
        grant_ref_atom(s,a,s->stores[0],grant)};
    return request(a,s,request_id,"revoke-grant",args,2);
}
static Atom *rotate_principal_request(Arena *a,Service *s,const char *request_id,uint32_t uid) {
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,uid)},1);
    Atom *args[]={account};
    return request(a,s,request_id,"rotate-principal",args,1);
}
static void decision_index_collision_control(void) {
    Service service={0};
    char first[512][32]={{0}},left[32]={0},right[32]={0};
    uint32_t uid=4242;
    Principal caller={.uid=uid};
    for (unsigned i=0;i<=512 && !right[0];++i) {
        char id[32]; snprintf(id,sizeof(id),"collision-%u",i);
        size_t slot=(size_t)decision_hash(caller,id)&511u;
        if (first[slot][0]) {
            snprintf(left,sizeof(left),"%s",first[slot]);
            snprintf(right,sizeof(right),"%s",id);
        } else snprintf(first[slot],sizeof(first[slot]),"%s",id);
    }
    check(left[0] && right[0] && strcmp(left,right));
    Decision a={.caller=caller,.root=true},b={.caller=caller,.root=true};
    snprintf(a.id,sizeof(a.id),"%s",left);
    snprintf(b.id,sizeof(b.id),"%s",right);
    check(decision_restore(&service,&a) && decision_restore(&service,&b));
    check(find_decision(&service,caller,left) && find_decision(&service,caller,right));
    check(!find_decision(&service,caller,"collision-absent"));
    for (unsigned i=0;i<500;++i) {
        Decision next={.caller=caller,.root=true};
        snprintf(next.id,sizeof(next.id),"growth-%u",i);
        check(decision_restore(&service,&next));
    }
    check(service.decision_slot_capacity>512 &&
          find_decision(&service,caller,left) && find_decision(&service,caller,right));
    free(service.decisions); free(service.decision_slots);
}
static void principal_index_growth_control(void) {
    Service service={0}; enum { PRINCIPALS=600 };
    for (uint32_t i=1;i<=PRINCIPALS;++i) {
        Principal principal=legacy_principal(&service,50000+i);
        check(principal.uid==50000+i && principal.epoch==i &&
              find_principal(&service,principal.uid) &&
              principal_same(find_principal(&service,principal.uid)->current,principal));
    }
    check(service.principal_slot_capacity>16 && !find_principal(&service,49999));
    Principal before=current_principal(&service,50300);
    PreparedCommit prepared={0};
    check(!principal_prepare(&service,before.uid,true,&prepared) && prepared.has_principal);
    Principal after=prepared.principal.current;
    check(principal_install(&service,&prepared) && after.epoch==PRINCIPALS+1 &&
          principal_same(current_principal(&service,before.uid),after) &&
          !principal_same(before,after) &&
          current_principal(&service,50301).epoch==301);
    free(service.principals); free(service.principal_slots);
}
static void grant_index_growth_control(void) {
    Store store={0};
    enum { GRANTS=600 };
    for (uint32_t i=1;i<=GRANTS;++i) {
        Bytes form={0}; uint64_t issued=0;
        Principal grantee={.uid=10000+i,.epoch=1};
        check(grant_issue(&store,grantee,(Principal){0},RIGHT_APPEND,0,false,false,0,0,
                          &form,false,&issued));
        check(issued==i && find_grant_ref(&store,i,i)==&store.grants[i-1]);
    }
    check(store.grant_slot_capacity>8 && !find_grant_ref(&store,GRANTS+1,GRANTS+1));
    for (uint64_t i=1;i<=GRANTS;++i) {
        Grant *grant=find_grant_ref(&store,i,i);
        check(grant && grant->grantee.uid==10000+i && grant->grantee.epoch==1);
    }
    for (size_t i=0;i<store.grant_count;++i) bytes_free(&store.grants[i].form);
    free(store.grants); free(store.grant_slots);
}
static void grant_restore_structure_controls(Arena *a) {
    Service service={0};
    Principal parentPrincipal=test_principal(&service,101);
    Principal childPrincipal=test_principal(&service,102);
    Store store={.grant_generation=2,.grant_clock_restored=true};
    Atom *pattern=form(a,"bundle",(Atom *[]){atom_var(a,"value")},1);
    Bytes parentForm={0},childForm={0};
    check(grant_form_bytes(pattern,atom_symbol(a,"none"),&parentForm));
    check(bytes_copy(&childForm,parentForm.data,parentForm.size));
    Grant child={.id=2,.generation=2,.parent_id=1,.parent_generation=1,
        .grantee=childPrincipal,.issuer=parentPrincipal,.rights=RIGHT_APPEND,
        .ticket_rights=RIGHT_APPEND};
    Grant parent={.id=1,.generation=1,.grantee=parentPrincipal,.rights=RIGHT_APPEND,
        .ticket_rights=RIGHT_APPEND,.delegable=true};
    /* Checkpoints need not sort grants topologically: the sealed graph check
     * validates exact parent references after every immutable row is loaded. */
    check(grant_restore(&store,&child,&childForm));
    check(grant_restore(&store,&parent,&parentForm));
    check(grants_consistent(&store));
    Grant *storedChild=find_grant_ref(&store,2,2);
    Grant *storedParent=find_grant_ref(&store,1,1);
    check(storedChild && storedParent &&
          grant_authority(&service,&store,storedChild,childPrincipal).rights==RIGHT_APPEND);

    storedChild->rights=RIGHT_WRITE;
    check(!grants_consistent(&store));
    storedChild->rights=RIGHT_APPEND;
    Bytes inherited=storedChild->form; storedChild->form=(Bytes){0};
    check(!grants_consistent(&store));
    storedChild->form=inherited;
    storedChild->direct_default=true;
    check(!grants_consistent(&store));
    storedChild->direct_default=false;
    storedChild->parent_generation=2;
    check(!grants_consistent(&store));
    storedChild->parent_generation=1;
    storedParent->revoked=true;
    check(grants_consistent(&store) &&
          !grant_authority(&service,&store,storedChild,childPrincipal).generation);
    storedParent->revoked=false;
    Bytes duplicateForm={0};
    check(bytes_copy(&duplicateForm,storedParent->form.data,storedParent->form.size));
    check(!grant_restore(&store,&parent,&duplicateForm));
    check(!duplicateForm.data && grants_consistent(&store));
    Grant noncanonical=parent; noncanonical.id=3; noncanonical.generation=2;
    Bytes noncanonicalForm={0};
    check(bytes_copy(&noncanonicalForm,storedParent->form.data,storedParent->form.size));
    check(!grant_restore(&store,&noncanonical,&noncanonicalForm));
    check(!noncanonicalForm.data && grants_consistent(&store));

    for (size_t i=0;i<store.grant_count;++i) bytes_free(&store.grants[i].form);
    free(store.grants); free(store.grant_slots);
    free(service.principals); free(service.principal_slots);
}
static void account_origin_decision_controls(Arena *a) {
    Principal originPrincipal={.uid=101,.epoch=1};
    Decision origin={.caller=originPrincipal,.source=1,.grant=2,.generation=2,
        .required=RIGHT_APPEND,
        .origin="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .effect_digest="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
    snprintf(origin.id,sizeof(origin.id),"origin-control");
    check(decision_stored_valid(&origin));
    Decision missingDigest=origin; missingDigest.effect_digest[0]=0;
    check(!decision_stored_valid(&missingDigest));
    Decision malformed=origin; malformed.origin[0]='G';
    check(!decision_stored_valid(&malformed));
    Decision wrongOperation=origin; wrongOperation.required=RIGHT_WRITE;
    check(!decision_stored_valid(&wrongOperation));
    Decision root=origin; root.root=true; root.source=root.grant=root.generation=0;
    root.required=0;
    check(!decision_stored_valid(&root));
    Decision ordinary={.caller=originPrincipal,.source=1,.grant=2,.generation=2,
        .required=RIGHT_APPEND};
    snprintf(ordinary.id,sizeof(ordinary.id),"ordinary-control");
    check(decision_stored_valid(&ordinary));

    Service service={.principal_generation=1};
    snprintf(service.uuid,sizeof(service.uuid),"0123456789abcdef0123456789abcdef");
    check(principal_restore(&service,originPrincipal));
    check(decision_restore(&service,&origin));
    Client account={.fd=-1,.principal=originPrincipal};
    Decision *stored=find_decision(&service,origin.caller,origin.id);
    check(stored && origin_reference(&service,&account,
        origin_ref_atom(&service,a,origin.caller.uid,origin.id,origin.origin))==stored);
    Atom *reference=origin_ref_atom(&service,a,origin.caller.uid,origin.id,origin.origin);
    Atom *args[]={reference};
    success(execute_client_wire(&service,a,&account,
        request(a,&service,"inspect-account-origin","origin",args,1)));
    Client owner={.fd=-1,.principal={.uid=999},.owner=true};
    success(execute_client_wire(&service,a,&owner,
        request(a,&service,"inspect-owner-origin","origin",args,1)));
    Client other=test_client(&service,origin.caller.uid+1);
    failure(execute_client_wire(&service,a,&other,
        request(a,&service,"inspect-other-origin","origin",args,1)),"missing-origin");
    Atom *forgedArgs[]={origin_ref_atom(&service,a,origin.caller.uid,origin.id,
        "00000000000000000000000000000000")};
    failure(execute_client_wire(&service,a,&account,
        request(a,&service,"inspect-forged-origin","origin",forgedArgs,1)),"missing-origin");
    free(service.decisions); free(service.decision_slots);
    free(service.principals); free(service.principal_slots);
}
static void unbound_member_controls(Arena *a) {
    Service service={.process_uid=101,.owner_uid=202};
    snprintf(service.uuid,sizeof(service.uuid),"0123456789abcdef0123456789abcdef");
    snprintf(service.boot,sizeof(service.boot),"fedcba9876543210fedcba9876543210");
    Decision root={.caller={.uid=101},.root=true};
    snprintf(root.id,sizeof(root.id),"root-collision");
    check(decision_restore(&service,&root));
    Client member={.fd=-1,.principal={.uid=101}};
    Atom *list=execute_client_wire(&service,a,&member,
        request(a,&service,"root-collision","list",NULL,0));
    success(list);
    check(shape(part(list,3),"catalog",4) && shape(part(part(list,3),3),"spaces",1));
    Atom *receiptArgs[]={atom_string(a,"root-collision")};
    Atom *receipt=execute_client_wire(&service,a,&member,
        request(a,&service,"unbound-receipt","receipt",receiptArgs,1));
    success(receipt); check(atom_is_symbol(part(receipt,3),"unknown-receipt"));
    Atom *fakeOrigin=origin_ref_atom(&service,a,member.principal.uid,"absent",
        "00000000000000000000000000000000");
    Atom *originArgs[]={fakeOrigin};
    failure(execute_client_wire(&service,a,&member,
        request(a,&service,"unbound-origin","origin",originArgs,1)),"missing-origin");
    failure(execute_client_wire(&service,a,&member,
        request(a,&service,"unbound-principal","principal-ref",NULL,0)),
        "authority-refused");
    Atom *openArgs[]={atom_string(a,"absent"),atom_symbol(a,"native"),atom_int(a,0)};
    failure(execute_client_wire(&service,a,&member,
        request(a,&service,"unbound-open","open",openArgs,3)),"missing-space");
    Atom *badParts[]={atom_string(a,"bad-service"),atom_symbol(a,"list"),
        atom_string(a,"00000000000000000000000000000000")};
    failure(execute_client_wire(&service,a,&member,form(a,"request",badParts,3)),
        "service-identity");
    check(!find_principal(&service,member.principal.uid));
    free(service.decisions); free(service.decision_slots);
}
static void legacy_v5_principal_migration_controls(Arena *a) {
    Service service={.max_facts=1000,.max_live_bytes=1024*1024};
    const char *uuid="0123456789abcdef0123456789abcdef";
    Atom *headerArgs[]={atom_string(a,uuid),atom_int(a,1)};
    check(restore_record(&service,a,form(a,"header",headerArgs,2),true));
    Atom *catalogArgs[]={atom_int(a,2),atom_int(a,0)};
    check(restore_record(&service,a,form(a,"catalog",catalogArgs,2),false));
    Atom *spaceArgs[]={atom_int(a,1),atom_string(a,"legacy"),atom_symbol(a,"native"),
        atom_int(a,0)};
    check(restore_record(&service,a,form(a,"restore-space",spaceArgs,4),false));
    Atom *clockArgs[]={atom_int(a,1),atom_int(a,1)};
    check(restore_record(&service,a,form(a,"restore-grant-clock",clockArgs,2),false));
    uint32_t uid=42424;
    Atom *grantArgs[]={atom_int(a,1),atom_int(a,1),atom_int(a,1),atom_int(a,0),
        atom_int(a,uid),atom_int(a,RIGHT_APPEND),atom_int(a,0),atom_symbol(a,"false"),
        atom_int(a,0),atom_int(a,0),atom_symbol(a,"true"),atom_symbol(a,"false"),
        atom_string(a,"")};
    check(restore_record(&service,a,form(a,"restore-grant-v4",grantArgs,13),false));
    Atom *sealArgs[]={atom_int(a,1),atom_int(a,0),atom_int(a,0),atom_int(a,0),
        atom_int(a,0),atom_int(a,1)};
    check(restore_record(&service,a,form(a,"sealed-v5",sealArgs,6),false));
    Principal migrated=current_principal(&service,uid);
    Grant *grant=find_grant(service.stores[0],migrated);
    check(!service.restoring && migrated.epoch==1 && service.principal_generation==1 &&
          grant && principal_same(grant->grantee,migrated));
    store_free(service.stores[0]);
    free(service.principals); free(service.principal_slots);

    Service malformed={0};
    check(restore_record(&malformed,a,form(a,"header",headerArgs,2),true));
    check(restore_record(&malformed,a,form(a,"catalog",catalogArgs,2),false));
    Atom *principalClock[]={atom_int(a,2)};
    check(restore_record(&malformed,a,
        form(a,"restore-principal-clock",principalClock,1),false));
    Atom *first[]={atom_int(a,10),atom_int(a,1)};
    Atom *duplicateEpoch[]={atom_int(a,11),atom_int(a,1)};
    check(restore_record(&malformed,a,form(a,"restore-principal",first,2),false));
    check(!restore_record(&malformed,a,
        form(a,"restore-principal",duplicateEpoch,2),false));
    free(malformed.principals); free(malformed.principal_slots);
}
static void principal_recovery_refusal_controls(Arena *a) {
    const char *uuid="0123456789abcdef0123456789abcdef";
    Atom *headerArgs[]={atom_string(a,uuid),atom_int(a,1)};
    Atom *catalogArgs[]={atom_int(a,1),atom_int(a,0)};

    Service mixed={0};
    check(restore_record(&mixed,a,form(a,"header",headerArgs,2),true));
    check(restore_record(&mixed,a,form(a,"catalog",catalogArgs,2),false));
    Atom *zeroClock[]={atom_int(a,0)};
    check(restore_record(&mixed,a,
        form(a,"restore-principal-clock",zeroClock,1),false));
    Atom *legacySeal[]={atom_int(a,0),atom_int(a,0),atom_int(a,0),atom_int(a,0),
        atom_int(a,0),atom_int(a,0)};
    check(!restore_record(&mixed,a,form(a,"sealed-v5",legacySeal,6),false));
    free(mixed.principals); free(mixed.principal_slots);

    Service missingDecision={0};
    check(restore_record(&missingDecision,a,form(a,"header",headerArgs,2),true));
    check(restore_record(&missingDecision,a,form(a,"catalog",catalogArgs,2),false));
    Atom *oneClock[]={atom_int(a,1)};
    Atom *binding[]={atom_int(a,303),atom_int(a,1)};
    check(restore_record(&missingDecision,a,
        form(a,"restore-principal-clock",oneClock,1),false));
    check(restore_record(&missingDecision,a,form(a,"restore-principal",binding,2),false));
    Atom *listRequest=request(a,&missingDecision,"missing-decision","list",NULL,0);
    Atom *listAnswer=reply(a,listRequest,true,atom_symbol(a,"unused"));
    Bytes rawRequest={0},rawAnswer={0};
    check(wire(listRequest,&rawRequest) && wire(listAnswer,&rawAnswer));
    Atom *receiptArgs[]={atom_int(a,303),atom_int(a,1),
        atom_string_n(a,(const char *)rawRequest.data,rawRequest.size),
        atom_string_n(a,(const char *)rawAnswer.data,rawAnswer.size)};
    check(!restore_record(&missingDecision,a,
        form(a,"receipt-from-v2",receiptArgs,4),false));
    bytes_free(&rawRequest); bytes_free(&rawAnswer);
    free(missingDecision.principals); free(missingDecision.principal_slots);

    Service unusedPending={.next_store=1,.max_facts=1000,
        .max_live_bytes=1024*1024,.process_uid=404,.owner_uid=404};
    snprintf(unusedPending.uuid,sizeof(unusedPending.uuid),"%s",uuid);
    Atom *openArgs[]={atom_string(a,"pending"),atom_symbol(a,"native"),atom_int(a,0)};
    Atom *openRequest=request(a,&unusedPending,"unused-pending","open",openArgs,3);
    Atom *openAnswer=reply(a,openRequest,true,atom_symbol(a,"unused"));
    check(wire(openRequest,&rawRequest) && wire(openAnswer,&rawAnswer));
    Atom *eventArgs[]={atom_int(a,404),atom_int(a,0),atom_int(a,1),atom_int(a,0),
        atom_int(a,0),atom_int(a,0),atom_int(a,0),atom_int(a,0),
        atom_symbol(a,"none"),atom_symbol(a,"none"),atom_int(a,505),atom_int(a,1),
        atom_string_n(a,(const char *)rawRequest.data,rawRequest.size),
        atom_string_n(a,(const char *)rawAnswer.data,rawAnswer.size)};
    check(!restore_record(&unusedPending,a,
        form(a,"event-authorized-v5",eventArgs,14),false));
    bytes_free(&rawRequest); bytes_free(&rawAnswer);
    for (size_t i=0;i<STORES;++i) store_free(unusedPending.stores[i]);
    for (size_t i=0;i<unusedPending.receipt_count;++i)
        receipt_free(&unusedPending.receipts[i]);
    free(unusedPending.decisions); free(unusedPending.decision_slots);
    free(unusedPending.principals); free(unusedPending.principal_slots);
}
static void grant_generation_publication(Service *s,Arena *a,const char *directory) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    Store *st=s->stores[0]; uint32_t member_uid=s->process_uid+1;
    success(execute_wire(s,a,grant_request(a,s,"grant-read-1",member_uid,RIGHT_READ)));
    Principal memberPrincipal=current_principal(s,member_uid);
    Grant *grant=find_grant(st,memberPrincipal);
    check(grant && grant->generation==1 && st->grant_generation==1);

    Client member={.fd=-1,.principal=memberPrincipal};
    Atom *query_args[]={atom_int(a,st->id),atom_var(a,"x"),atom_var(a,"x"),
                        atom_int(a,1),atom_int(a,10000)};
    Atom *query_request=request(a,s,"member-query","query",query_args,5);
    Bytes query_raw={0}; check(wire(query_request,&query_raw));
    success(dispatch(s,&member,a,query_request,query_raw)); bytes_free(&query_raw);
    Snapshot *snapshot=find_snapshot(s,1);
    check(snapshot && principal_same(snapshot->holder,memberPrincipal) &&
          snapshot->authority_generation==1);

    /* Cursor identity is account-scoped even though the owner can query the
     * same underlying store directly. */
    Atom *fetch=fetch_request(a,s,"owner-cannot-borrow",snapshot->id);
    Bytes fetch_raw={0}; check(wire(fetch,&fetch_raw));
    Client owner={.fd=-1,.principal=root_principal(s),.owner=true};
    failure(dispatch(s,&owner,a,fetch,fetch_raw),"expired-or-closed");
    bytes_free(&fetch_raw);

    Atom *watch_args[]={atom_int(a,st->id),atom_int(a,st->revision),atom_int(a,10000)};
    Atom *watch=request(a,s,"member-watch","watch",watch_args,3);
    check(wire(watch,&member.input));
    check(!dispatch(s,&member,a,watch,member.input) && member.watching &&
          member.watch_authority_generation==1);

    success(execute_wire(s,a,grant_request(a,s,"revoke-read",member_uid,0)));
    success(execute_wire(s,a,grant_request(a,s,"grant-read-2",member_uid,RIGHT_READ)));
    grant=find_grant(st,memberPrincipal);
    check(grant && grant->generation==3 && st->grant_generation==3);

    fetch=fetch_request(a,s,"stale-member-cursor",snapshot->id);
    check(wire(fetch,&fetch_raw));
    failure(dispatch(s,&member,a,fetch,fetch_raw),"expired-or-closed");
    bytes_free(&fetch_raw);
    check(client_wake(s,&member) && !member.watching && member.output.size>8);
    Bytes wake_reply={member.output.data+8,member.output.size-8}; Atom *wake_answer=NULL;
    check(decode(a,wake_reply,&wake_answer)); failure(wake_answer,"missing-space");
    client_close(s,&member);

    check(compact(s));
    uint64_t before=st->grant_generation;
    release(s); initialize(s,directory); st=s->stores[0];
    memberPrincipal=current_principal(s,member_uid);
    grant=find_grant(st,memberPrincipal);
    check(before==3 && st->grant_generation==before && grant && grant->generation==before);
    success(execute_wire(s,a,grant_request(a,s,"grant-read-3",member_uid,RIGHT_READ)));
    grant=find_grant(st,memberPrincipal);
    check(grant && grant->generation==before+1 && st->grant_generation==before+1);
}
static Atom *authority_argument(Service *s,Arena *a,Store *st,Grant *grant) {
    Atom *reference=grant_ref_atom(s,a,st,grant);
    return form(a,"authority",&reference,1);
}
static void explicit_read_grant_publication(Service *s,Arena *a) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    success(execute_wire(s,a,batch_request(a,s,"seed-explicit-read",true)));
    Store *st=s->stores[0]; uint32_t member_uid=s->process_uid+21;
    success(execute_wire(s,a,grant_root_request(a,s,"explicit-read-root",member_uid,
        RIGHT_OBSERVE|RIGHT_READ,0,false)));
    Grant *grant=find_grant_ref(st,st->grant_generation,st->grant_generation);
    check(grant && !grant->direct_default);
    uint64_t old_id=grant->id;
    Principal memberPrincipal=current_principal(s,member_uid);
    Client member={.fd=-1,.principal=memberPrincipal};

    Atom *plain_args[]={atom_int(a,st->id),atom_var(a,"x"),atom_var(a,"x"),
        atom_int(a,2),atom_int(a,10000)};
    failure(execute_client_wire(s,a,&member,
        request(a,s,"ambient-read-refused","query",plain_args,5)),"missing-space");

    Atom *authority=authority_argument(s,a,st,grant);
    Atom *query_args[]={atom_int(a,st->id),atom_var(a,"x"),atom_var(a,"x"),
        atom_int(a,2),atom_int(a,10000),authority};
    success(execute_client_wire(s,a,&member,
        request(a,s,"explicit-read","query",query_args,6)));
    Snapshot *snapshot=find_snapshot(s,1);
    check(snapshot && principal_same(snapshot->holder,memberPrincipal) &&
          snapshot->authority_grant==grant->id &&
          snapshot->authority_generation==grant->generation);
    success(execute_client_wire(s,a,&member,
        fetch_request(a,s,"explicit-read-page",snapshot->id)));

    Atom *watch_args[]={atom_int(a,st->id),atom_int(a,st->revision),atom_int(a,10000),authority};
    Atom *watch=request(a,s,"explicit-read-watch","watch",watch_args,4);
    check(wire(watch,&member.input));
    check(!dispatch(s,&member,a,watch,member.input) && member.watching &&
          member.watch_authority_grant==grant->id &&
          member.watch_authority_generation==grant->generation);

    success(execute_wire(s,a,revoke_grant_request(a,s,"revoke-explicit-read",grant)));
    failure(execute_client_wire(s,a,&member,
        fetch_request(a,s,"revoked-explicit-page",snapshot->id)),"expired-or-closed");
    check(client_wake(s,&member) && !member.watching && member.output.size>8);
    Bytes wake_reply={member.output.data+8,member.output.size-8}; Atom *wake_answer=NULL;
    check(decode(a,wake_reply,&wake_answer)); failure(wake_answer,"missing-space");
    client_close(s,&member);

    success(execute_wire(s,a,grant_root_request(a,s,"fresh-explicit-read",member_uid,
        RIGHT_OBSERVE|RIGHT_READ,0,false)));
    Grant *fresh=find_grant_ref(st,st->grant_generation,st->grant_generation);
    check(fresh && fresh->id!=old_id);
    member=(Client){.fd=-1,.principal=memberPrincipal};
    failure(execute_client_wire(s,a,&member,
        fetch_request(a,s,"regrant-does-not-revive-page",snapshot->id)),"expired-or-closed");
    failure(execute_client_wire(s,a,&member,
        request(a,s,"old-explicit-read","query",query_args,6)),"missing-space");
    query_args[5]=authority_argument(s,a,st,fresh);
    success(execute_client_wire(s,a,&member,
        request(a,s,"fresh-explicit-read-query","query",query_args,6)));
    client_close(s,&member);

    uint32_t observer_uid=member_uid+1;
    success(execute_wire(s,a,grant_root_request(a,s,"explicit-observe-root",observer_uid,
        RIGHT_OBSERVE,0,false)));
    Grant *observer=find_grant_ref(st,st->grant_generation,st->grant_generation);
    Client observer_client=test_client(s,observer_uid);
    Atom *observer_authority=authority_argument(s,a,st,observer);
    Atom *describe_args[]={atom_int(a,st->id),observer_authority};
    success(execute_client_wire(s,a,&observer_client,
        request(a,s,"explicit-observe-describe","describe",describe_args,2)));
    Atom *count_args[]={atom_int(a,st->id),atom_var(a,"x"),observer_authority};
    failure(execute_client_wire(s,a,&observer_client,
        request(a,s,"explicit-observe-cannot-read","count",count_args,3)),"forbidden");
}
static void ticket_publication(Service *s,Arena *a,const char *directory) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    Store *st=s->stores[0]; uint32_t member_uid=s->process_uid+1;
    success(execute_wire(s,a,grant_ticketed_request(a,s,"grant-write",member_uid,
        RIGHT_WRITE,RIGHT_WRITE)));
    publication(s,a,issue_ticket_request(a,s,"issue-ticket-1",member_uid,RIGHT_WRITE));
    Ticket *ticket=find_ticket(st,1);
    Principal memberPrincipal=current_principal(s,member_uid);
    check(ticket && ticket->status==TICKET_LIVE &&
          principal_same(ticket->holder,memberPrincipal) &&
          ticket->grant_generation==find_grant(st,memberPrincipal)->generation);

    Client member={.fd=-1,.principal=memberPrincipal};
    Atom *plain=batch_request(a,s,"ticket-bypass",false); Bytes plainRaw={0};
    check(wire(plain,&plainRaw)); failure(dispatch(s,&member,a,plain,plainRaw),"ticket-required");
    bytes_free(&plainRaw);
    Atom *use=batch_ticket_request(a,s,"use-ticket-1",ticket->id); Bytes raw={0};
    check(wire(use,&raw)); reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=dispatch(s,&member,a,use,raw);
    check(write_started && !after_write_allocations && !s->poisoned); success(answer);
    reset_faults();
    check(st->revision==1 && ticket->status==TICKET_SPENT && !strcmp(ticket->spent_id,"use-ticket-1"));
    success(dispatch(s,&member,a,use,raw));
    Atom *copy=batch_ticket_request(a,s,"copied-ticket",ticket->id); Bytes copyRaw={0};
    check(wire(copy,&copyRaw)); failure(dispatch(s,&member,a,copy,copyRaw),"ticket-unavailable");
    bytes_free(&copyRaw);

    success(execute_wire(s,a,issue_ticket_request(a,s,"issue-ticket-2",member_uid,RIGHT_WRITE)));
    ticket=find_ticket(st,2); check(ticket && ticket->status==TICKET_LIVE);
    Atom *second=batch_ticket_request(a,s,"use-ticket-2",ticket->id); Bytes secondRaw={0};
    check(wire(second,&secondRaw)); reset_faults(); observed_journal=s->journal; fail_event=true;
    failure(dispatch(s,&member,a,second,secondRaw),"limit");
    check(!write_started && ticket->status==TICKET_LIVE &&
          !find_decision(s,memberPrincipal,"use-ticket-2"));
    reset_faults(); success(dispatch(s,&member,a,second,secondRaw)); bytes_free(&secondRaw);
    check(ticket->status==TICKET_SPENT && st->revision==2);

    check(compact(s)); release(s); initialize(s,directory); st=s->stores[0];
    ticket=find_ticket(st,1);
    check(ticket && ticket->status==TICKET_SPENT && !strcmp(ticket->spent_id,"use-ticket-1"));
    success(dispatch(s,&member,a,use,raw)); bytes_free(&raw);
    copy=batch_ticket_request(a,s,"copied-after-restart",ticket->id); check(wire(copy,&copyRaw));
    failure(dispatch(s,&member,a,copy,copyRaw),"ticket-unavailable"); bytes_free(&copyRaw);
}
/* Journal a delegation exactly as a service under the earlier subset rule
 * recorded it, which let a child drop a ticket requirement: the same request,
 * prepared commit and reply, with the commit admitted by that rule. */
static Atom *legacy_subset_delegation(Service *s,Arena *a,Client *client,Store *st,
                                      Grant *parent,const char *id,uint32_t uid,
                                      unsigned rights,unsigned ticket_rights) {
    Atom *request=delegate_grant_request(a,s,id,parent,uid,rights,ticket_rights,false);
    Bytes raw={0}; check(wire(request,&raw));
    PreparedCommit prepared={.has_grant_form=true};
    check(!parent->form.size ||
          bytes_copy(&prepared.grant_form,parent->form.data,parent->form.size));
    check(grant_reserve(st) && !principal_prepare(s,uid,false,&prepared));
    Grant predicted={.id=st->grant_generation+1,.generation=st->grant_generation+1};
    Atom *args[]={grant_ref_atom(s,a,st,&predicted),part(request,6),rights_atom(a,rights),
        rights_atom(a,ticket_rights),atom_symbol(a,"false")};
    s->replaying=true;
    Atom *answer=commit(s,a,client,request,raw,
        reply(a,request,true,form(a,"delegated",args,5)),&prepared);
    s->replaying=false;
    bytes_free(&raw);
    return answer;
}
static void attenuating_lineage_publication(Service *s,Arena *a,const char *directory) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    Store *st=s->stores[0];
    uint32_t accountA=s->process_uid+10, accountB=s->process_uid+11;
    uint32_t accountC=s->process_uid+12, accountD=s->process_uid+13;
    success(execute_wire(s,a,grant_root_request(a,s,"root-a",accountA,RIGHT_WRITE,
        RIGHT_APPEND|RIGHT_WRITE,true)));
    Grant *root=find_grant_ref(st,1,1);
    Principal principalA=current_principal(s,accountA);
    check(root && !root->direct_default && principal_same(root->grantee,principalA) &&
          root->delegable);

    Client aClient={.fd=-1,.principal=principalA};
    success(execute_client_wire(s,a,&aClient,delegate_grant_request(a,s,"a-to-b",root,
        accountB,RIGHT_READ|RIGHT_APPEND,RIGHT_APPEND,true)));
    Grant *middle=find_grant_ref(st,2,2);
    Principal principalB=current_principal(s,accountB);
    check(middle && principal_same(middle->issuer,principalA) &&
          principal_same(middle->grantee,principalB) && middle->parent_id==root->id &&
          middle->parent_generation==root->generation && middle->delegable);

    Client bClient={.fd=-1,.principal=principalB};
    success(execute_client_wire(s,a,&bClient,delegate_grant_request(a,s,"b-to-c",middle,
        accountC,RIGHT_APPEND,RIGHT_APPEND,false)));
    Grant *leaf=find_grant_ref(st,3,3);
    Principal principalC=current_principal(s,accountC);
    check(leaf && principal_same(leaf->issuer,principalB) &&
          principal_same(leaf->grantee,principalC) && leaf->parent_id==middle->id &&
          same_form(root->form,middle->form) && same_form(middle->form,leaf->form));
    AuthorityView leafAuthority=grant_authority(s,st,leaf,principalC);
    check(leafAuthority.grant==leaf->id && leafAuthority.rights==RIGHT_APPEND &&
          !leafAuthority.delegable);

    size_t beforeCount=st->grant_count; uint64_t beforeGeneration=st->grant_generation;
    failure(execute_client_wire(s,a,&bClient,delegate_grant_request(a,s,"b-escalates",
        middle,accountD,RIGHT_WRITE,RIGHT_APPEND,false)),"attenuation");
    check(st->grant_count==beforeCount && st->grant_generation==beforeGeneration);
    Client cClient={.fd=-1,.principal=principalC};
    failure(execute_client_wire(s,a,&cClient,delegate_grant_request(a,s,"c-delegates",
        leaf,accountD,RIGHT_APPEND,RIGHT_APPEND,false)),"forbidden");

    success(execute_client_wire(s,a,&aClient,delegate_grant_request(a,s,"a-to-d",root,
        accountD,RIGHT_APPEND,RIGHT_APPEND,false)));
    Principal principalD=current_principal(s,accountD);
    Grant *sibling=find_grant_ref(st,4,4);
    check(sibling && principal_same(sibling->grantee,principalD));
    success(execute_client_wire(s,a,&aClient,delegate_grant_request(a,s,"a-to-d-2",root,
        accountD,RIGHT_APPEND,RIGHT_APPEND,false)));
    root=find_grant_ref(st,1,1); middle=find_grant_ref(st,2,2);
    leaf=find_grant_ref(st,3,3); sibling=find_grant_ref(st,4,4);
    Grant *sibling2=find_grant_ref(st,5,5);
    check(root && middle && leaf && sibling && sibling2 &&
          principal_same(sibling2->grantee,principalD));
    success(execute_wire(s,a,issue_ticket_v2_request(a,s,"ticket-c",leaf,RIGHT_APPEND)));
    Ticket *leafTicket=find_ticket(st,1);
    check(leafTicket && leafTicket->grant_id==leaf->id &&
          leafTicket->grant_generation==leaf->generation &&
          principal_same(leafTicket->holder,principalC));
    success(execute_wire(s,a,issue_ticket_v2_request(a,s,"ticket-d",sibling,RIGHT_APPEND)));
    Ticket *siblingTicket=find_ticket(st,2);
    check(siblingTicket && principal_same(siblingTicket->holder,principalD));

    Atom *appendC=append_authorized_request(a,s,"c-append",leaf,leafTicket,"from-c");
    success(execute_client_wire(s,a,&cClient,appendC));
    check(leafTicket->status==TICKET_SPENT && siblingTicket->status==TICKET_LIVE);
    Client dClient={.fd=-1,.principal=principalD};
    failure(execute_client_wire(s,a,&dClient,
        append_authorized_request(a,s,"wrong-leaf",sibling2,siblingTicket,"forged")),
        "ticket-unavailable");
    check(siblingTicket->status==TICKET_LIVE);
    failure(execute_client_wire(s,a,&dClient,
        append_authorized_request(a,s,"wrong-grantee",leaf,NULL,"forged")),"missing-space");

    check(compact(s)); release(s); initialize(s,directory); st=s->stores[0];
    root=find_grant_ref(st,1,1); middle=find_grant_ref(st,2,2);
    leaf=find_grant_ref(st,3,3); sibling=find_grant_ref(st,4,4);
    sibling2=find_grant_ref(st,5,5);
    leafTicket=find_ticket(st,1); siblingTicket=find_ticket(st,2);
    check(root && middle && leaf && sibling && sibling2 && grants_consistent(st));
    success(execute_client_wire(s,a,&cClient,appendC));
    success(execute_client_wire(s,a,&dClient,
        append_authorized_request(a,s,"d-append",sibling,siblingTicket,"from-d")));
    check(siblingTicket->status==TICKET_SPENT);

    success(execute_client_wire(s,a,&bClient,
        revoke_grant_request(a,s,"b-revokes-c",leaf)));
    check(leaf->revoked && grant_authority(s,st,middle,principalB).generation &&
          !grant_authority(s,st,leaf,principalC).generation);
    failure(execute_client_wire(s,a,&cClient,
        append_authorized_request(a,s,"old-c",leaf,NULL,"old")),"missing-space");
    /* Delegation never gives a child more than its parent can do: a child of
     * a grant whose appends need tickets cannot drop that requirement. */
    beforeCount=st->grant_count; beforeGeneration=st->grant_generation;
    failure(execute_client_wire(s,a,&bClient,delegate_grant_request(a,s,"b-to-c-free",
        middle,accountC,RIGHT_APPEND,0,false)),"attenuation");
    check(st->grant_count==beforeCount && st->grant_generation==beforeGeneration);
    success(execute_client_wire(s,a,&bClient,delegate_grant_request(a,s,"b-to-c-again",
        middle,accountC,RIGHT_APPEND,RIGHT_APPEND,false)));
    Grant *freshLeaf=find_grant_ref(st,st->grant_generation,st->grant_generation);
    uint64_t freshLeafId=st->grant_generation;
    check(freshLeaf && freshLeaf->id!=leaf->id &&
          principal_same(freshLeaf->grantee,principalC) &&
          freshLeaf->ticket_rights==RIGHT_APPEND);
    root=find_grant_ref(st,1,1); middle=find_grant_ref(st,2,2);
    leaf=find_grant_ref(st,3,3); sibling=find_grant_ref(st,4,4);
    freshLeaf=find_grant_ref(st,freshLeafId,freshLeafId);
    check(root && middle && leaf && sibling && freshLeaf);
    failure(execute_client_wire(s,a,&cClient,
        append_authorized_request(a,s,"new-c-free",freshLeaf,NULL,"free")),"ticket-required");
    success(execute_wire(s,a,issue_ticket_v2_request(a,s,"ticket-new-c",freshLeaf,RIGHT_APPEND)));
    Ticket *freshTicket=find_ticket(st,st->ticket_generation);
    freshLeaf=find_grant_ref(st,freshLeafId,freshLeafId);
    check(freshTicket && freshLeaf && freshTicket->grant_id==freshLeafId &&
          principal_same(freshTicket->holder,principalC));
    success(execute_client_wire(s,a,&cClient,
        append_authorized_request(a,s,"new-c",freshLeaf,freshTicket,"from-new-c")));
    check(find_ticket(st,freshTicket->id)->status==TICKET_SPENT);

    success(execute_wire(s,a,revoke_grant_request(a,s,"revoke-root-a",root)));
    check(root->revoked && !grant_authority(s,st,freshLeaf,principalC).generation &&
          !grant_authority(s,st,sibling,principalD).generation);
    failure(execute_client_wire(s,a,&cClient,
        append_authorized_request(a,s,"revoked-c",leaf,NULL,"late")),"missing-space");

    success(execute_wire(s,a,grant_root_request(a,s,"root-a-again",accountA,RIGHT_WRITE,
        RIGHT_APPEND|RIGHT_WRITE,true)));
    Grant *freshRoot=find_grant_ref(st,st->grant_generation,st->grant_generation);
    check(freshRoot && freshRoot->id!=root->id);
    failure(execute_client_wire(s,a,&cClient,
        append_authorized_request(a,s,"regranted-c",leaf,NULL,"late")),"missing-space");
    check(space_length64(st->space)==3);

    /* Requirements are closed along the implication of rights: a ticket for
     * each append beside a held write requires a ticket for write, since a
     * write batch adds rows too. The owner can mint that write ticket. */
    uint32_t accountG=s->process_uid+16,accountH=s->process_uid+17;
    success(execute_wire(s,a,grant_ticketed_request(a,s,"grant-g-write",accountG,
        RIGHT_WRITE,RIGHT_APPEND)));
    Principal principalG=current_principal(s,accountG);
    Client gClient={.fd=-1,.principal=principalG};
    uint64_t rowsBefore=space_length64(st->space);
    failure(execute_client_wire(s,a,&gClient,batch_request(a,s,"g-free-add",true)),
        "ticket-required");
    check(space_length64(st->space)==rowsBefore);
    success(execute_wire(s,a,issue_ticket_request(a,s,"ticket-g-write",accountG,RIGHT_WRITE)));
    Ticket *gTicket=find_ticket(st,st->ticket_generation);
    check(gTicket && gTicket->right==RIGHT_WRITE && principal_same(gTicket->holder,principalG));
    uint64_t gTicketId=gTicket->id;
    success(execute_client_wire(s,a,&gClient,batch_ticket_request(a,s,"g-ticketed",gTicketId)));
    check(find_ticket(st,gTicketId)->status==TICKET_SPENT && space_length64(st->space)==rowsBefore);
    /* Adding a requirement only attenuates: a ticket-free delegable grant may
     * delegate a child whose appends need tickets, which then binds it. */
    success(execute_wire(s,a,grant_root_request(a,s,"root-h-open",accountH,
        RIGHT_READ|RIGHT_APPEND,0,true)));
    uint64_t openRootId=st->grant_generation;
    Grant *openRoot=find_grant_ref(st,openRootId,openRootId);
    Client hClient={.fd=-1,.principal=current_principal(s,accountH)};
    success(execute_client_wire(s,a,&hClient,delegate_grant_request(a,s,"h-to-g-ticketed",
        openRoot,accountG,RIGHT_APPEND,RIGHT_APPEND,false)));
    uint64_t addedChildId=st->grant_generation;
    Grant *addedChild=find_grant_ref(st,addedChildId,addedChildId);
    check(addedChild && addedChild->ticket_rights==RIGHT_APPEND &&
          grant_authority(s,st,addedChild,principalG).rights==RIGHT_APPEND);
    failure(execute_client_wire(s,a,&gClient,
        append_authorized_request(a,s,"g-child-free",addedChild,NULL,"free")),"ticket-required");
    check(space_length64(st->space)==rowsBefore);
    /* A journal written under the earlier rule holds a ticket-free child of a
     * ticketed grant. It replays as recorded history, from the event log and
     * from a checkpoint, and the recovered child confers no authority. */
    uint32_t accountI=s->process_uid+18;
    success(execute_wire(s,a,grant_root_request(a,s,"root-b-ticketed",accountB,
        RIGHT_READ|RIGHT_APPEND,RIGHT_APPEND,true)));
    uint64_t ticketedRootId=st->grant_generation;
    Grant *ticketedRoot=find_grant_ref(st,ticketedRootId,ticketedRootId);
    check(ticketedRoot && ticketedRoot->ticket_rights==RIGHT_APPEND);
    failure(execute_client_wire(s,a,&bClient,delegate_grant_request(a,s,"b-to-i-live",
        ticketedRoot,accountI,RIGHT_APPEND,0,false)),"attenuation");
    success(legacy_subset_delegation(s,a,&bClient,st,ticketedRoot,"b-to-i-legacy",accountI,
        RIGHT_APPEND,0));
    uint64_t legacyId=st->grant_generation;
    release(s); initialize(s,directory); st=s->stores[0];
    Grant *legacy=find_grant_ref(st,legacyId,legacyId);
    Principal principalI=current_principal(s,accountI);
    Client iClient={.fd=-1,.principal=principalI};
    check(legacy && !legacy->ticket_rights && !legacy->revoked && grants_consistent(st) &&
          !grant_authority(s,st,legacy,principalI).generation);
    failure(execute_client_wire(s,a,&iClient,
        append_authorized_request(a,s,"i-free",legacy,NULL,"free")),"missing-space");
    check(compact(s)); release(s); initialize(s,directory); st=s->stores[0];
    legacy=find_grant_ref(st,legacyId,legacyId);
    check(legacy && !legacy->ticket_rights && grants_consistent(st) &&
          !grant_authority(s,st,legacy,principalI).generation &&
          space_length64(st->space)==rowsBefore);

    uint32_t accountE=s->process_uid+14,accountF=s->process_uid+15;
    Atom *caller=form(a,"caller",NULL,0);
    Atom *patternRow=form(a,"lineage-row",(Atom *[]){caller,atom_var(a,"value")},2);
    Atom *pattern=form(a,"bundle",&patternRow,1);
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,accountE)},1);
    Atom *rootArgs[]={atom_int(a,st->id),account,rights_atom(a,RIGHT_APPEND),
        rights_atom(a,0),atom_symbol(a,"true"),pattern,atom_symbol(a,"none")};
    Client owner={.fd=-1,.principal=root_principal(s),.owner=true};
    success(execute_client_without_postwrite_allocation(s,a,&owner,
        request(a,s,"shaped-root","grant-root",rootArgs,7)));
    uint64_t shapedRootId=st->grant_generation;
    Grant *shapedRoot=find_grant_ref(st,shapedRootId,shapedRootId);
    check(shapedRoot && shapedRoot->form.size);
    Client eClient=test_client(s,accountE);
    success(execute_client_without_postwrite_allocation(s,a,&eClient,
        delegate_grant_request(a,s,"e-to-f",shapedRoot,accountF,RIGHT_APPEND,0,false)));
    Grant *shapedLeaf=find_grant_ref(st,st->grant_generation,st->grant_generation);
    shapedRoot=find_grant_ref(st,shapedRootId,shapedRootId);
    check(shapedLeaf && same_form(shapedRoot->form,shapedLeaf->form));
    Client fClient=test_client(s,accountF);
    Atom *fAccount=form(a,"account",(Atom *[]){atom_int(a,accountF)},1);
    Atom *row=form(a,"lineage-row",(Atom *[]){fAccount,atom_int(a,7)},2);
    Atom *bundle=form(a,"bundle",&row,1);
    success(execute_client_wire(s,a,&fClient,
        append_authorized_bundle_request(a,s,"shaped-append",shapedLeaf,NULL,bundle)));
    Atom *wrongAccount=form(a,"account",(Atom *[]){atom_int(a,accountE)},1);
    row=form(a,"lineage-row",(Atom *[]){wrongAccount,atom_int(a,8)},2);
    bundle=form(a,"bundle",&row,1);
    failure(execute_client_wire(s,a,&fClient,
        append_authorized_bundle_request(a,s,"shaped-forgery",shapedLeaf,NULL,bundle)),
        "shape-mismatch");
    check(space_length64(st->space)==4);
}
static void stable_principal_publication(Service *s,Arena *a,const char *directory) {
    success(execute_wire(s,a,open_request(a,s,"native")));
    Store *st=s->stores[0];
    uint32_t accountA=s->process_uid+31,accountB=s->process_uid+32;
    uint32_t accountC=s->process_uid+33;
    success(execute_wire(s,a,grant_root_request(a,s,"principal-root-a",accountA,
        RIGHT_READ|RIGHT_APPEND,RIGHT_APPEND,true)));
    Grant *root=find_grant_ref(st,1,1);
    Principal oldA=current_principal(s,accountA);
    Client aClient={.fd=-1,.principal=oldA};
    success(execute_client_wire(s,a,&aClient,delegate_grant_request(a,s,"principal-a-b",
        root,accountB,RIGHT_READ|RIGHT_APPEND,RIGHT_APPEND,false)));
    Grant *leafB=find_grant_ref(st,2,2);
    Principal oldB=current_principal(s,accountB);
    Client oldBClient={.fd=-1,.principal=oldB};
    Client oldBWriter={.fd=-1,.principal=oldB};
    success(execute_client_wire(s,a,&aClient,delegate_grant_request(a,s,"principal-a-c",
        root,accountC,RIGHT_READ,0,false)));
    Grant *leafC=find_grant_ref(st,3,3);
    Principal principalC=current_principal(s,accountC);
    check(grant_authority(s,st,leafB,oldB).rights==(RIGHT_OBSERVE|RIGHT_READ|RIGHT_APPEND));
    check(grant_authority(s,st,leafC,principalC).rights==(RIGHT_OBSERVE|RIGHT_READ));

    success(execute_wire(s,a,issue_ticket_v2_request(a,s,"principal-ticket-b",leafB,
        RIGHT_APPEND)));
    Ticket *oldTicket=find_ticket(st,1);
    check(oldTicket && ticket_available(st,oldTicket->id,oldB,leafB->id,
        leafB->generation,RIGHT_APPEND));

    Atom *queryArgs[]={atom_int(a,st->id),atom_var(a,"x"),atom_var(a,"x"),
        atom_int(a,1),atom_int(a,10000),authority_argument(s,a,st,leafB)};
    success(execute_client_wire(s,a,&oldBClient,
        request(a,s,"principal-snapshot","query",queryArgs,6)));
    Snapshot *snapshot=find_snapshot(s,1);
    check(snapshot && principal_same(snapshot->holder,oldB));
    Atom *watchArgs[]={atom_int(a,st->id),atom_int(a,st->revision),atom_int(a,10000),
        authority_argument(s,a,st,leafB)};
    Atom *watch=request(a,s,"principal-watch","watch",watchArgs,4);
    check(wire(watch,&oldBClient.input));
    check(!dispatch(s,&oldBClient,a,watch,oldBClient.input) && oldBClient.watching);

    Atom *caller=form(a,"caller",NULL,0),*origin=form(a,"origin",NULL,0);
    Atom *patternRow=form(a,"principal-origin",(Atom *[]){caller,origin,atom_var(a,"v")},3);
    Atom *pattern=form(a,"bundle",&patternRow,1);
    Atom *account=form(a,"account",(Atom *[]){atom_int(a,accountB)},1);
    Atom *grantArgs[]={atom_int(a,st->id),account,rights_atom(a,RIGHT_APPEND),
        pattern,atom_symbol(a,"none")};
    success(execute_wire(s,a,request(a,s,"principal-origin-grant","grant",grantArgs,5)));
    Grant *oldDirect=find_grant(st,oldB);
    check(oldDirect && principal_same(oldDirect->grantee,oldB));
    Atom *accountValue=form(a,"account",(Atom *[]){atom_int(a,accountB)},1);
    Atom *originRow=form(a,"principal-origin",
        (Atom *[]){accountValue,form(a,"origin",NULL,0),atom_int(a,1)},3);
    Atom *bundle=form(a,"bundle",&originRow,1);
    Atom *appendArgs[]={atom_int(a,st->id),bundle};
    success(execute_client_wire(s,a,&oldBWriter,
        request(a,s,"principal-reused-id","append",appendArgs,2)));
    Decision *oldDecision=find_decision(s,oldB,"principal-reused-id");
    check(oldDecision && oldDecision->origin[0]);
    Atom *oldOrigin=origin_ref_atom(s,a,oldB.uid,oldDecision->id,oldDecision->origin);

    success(execute_wire(s,a,rotate_principal_request(a,s,"rotate-principal-b",accountB)));
    Principal freshB=current_principal(s,accountB);
    check(freshB.uid==oldB.uid && freshB.epoch>oldB.epoch &&
          !principal_same(freshB,oldB));
    check(!grant_authority(s,st,leafB,oldB).generation &&
          !grant_authority(s,st,leafB,freshB).generation);
    check(!ticket_available(st,oldTicket->id,freshB,leafB->id,leafB->generation,
        RIGHT_APPEND));
    failure(execute_client_wire(s,a,&oldBClient,
        request(a,s,"stale-socket","principal-ref",NULL,0)),"authority-refused");
    check(client_wake(s,&oldBClient) && !oldBClient.watching && oldBClient.output.size>8);
    Bytes wakeReply={oldBClient.output.data+8,oldBClient.output.size-8};
    Atom *wakeAnswer=NULL; check(decode(a,wakeReply,&wakeAnswer));
    failure(wakeAnswer,"missing-space");
    client_close(s,&oldBClient);

    Client freshBClient={.fd=-1,.principal=freshB};
    failure(execute_client_wire(s,a,&freshBClient,
        fetch_request(a,s,"fresh-cannot-use-old-snapshot",snapshot->id)),
        "expired-or-closed");
    Atom *originArgs[]={oldOrigin};
    failure(execute_client_wire(s,a,&freshBClient,
        request(a,s,"fresh-cannot-read-old-origin","origin",originArgs,1)),
        "missing-origin");
    Client owner={.fd=-1,.principal=root_principal(s),.owner=true};
    Atom *audit=execute_client_wire(s,a,&owner,
        request(a,s,"owner-audits-old-origin","origin",originArgs,1));
    success(audit);
    Atom *auditValue=part(audit,3),*auditPrincipal=part(auditValue,2);
    check(shape(auditValue,"account-origin",5) &&
          shape(auditPrincipal,"principal-ref",4) &&
          part(auditPrincipal,3)->ground.ival==(int64_t)oldB.epoch);

    account=form(a,"account",(Atom *[]){atom_int(a,accountB)},1);
    grantArgs[1]=account;
    success(execute_wire(s,a,request(a,s,"principal-origin-regrant","grant",grantArgs,5)));
    Grant *freshDirect=find_grant(st,freshB);
    check(freshDirect && !principal_same(freshDirect->grantee,oldB));
    originRow=form(a,"principal-origin",
        (Atom *[]){accountValue,form(a,"origin",NULL,0),atom_int(a,2)},3);
    bundle=form(a,"bundle",&originRow,1); appendArgs[1]=bundle;
    success(execute_client_wire(s,a,&freshBClient,
        request(a,s,"principal-reused-id","append",appendArgs,2)));
    Decision *freshDecision=find_decision(s,freshB,"principal-reused-id");
    check(freshDecision && freshDecision!=oldDecision && freshDecision->origin[0] &&
          find_receipt(s,oldB,"principal-reused-id") &&
          find_receipt(s,freshB,"principal-reused-id"));

    success(execute_wire(s,a,rotate_principal_request(a,s,"rotate-principal-a",accountA)));
    Principal freshA=current_principal(s,accountA);
    root=find_grant_ref(st,1,1); leafC=find_grant_ref(st,3,3);
    check(freshA.uid==oldA.uid && freshA.epoch>oldA.epoch &&
          principal_same(leafC->issuer,oldA) &&
          !principal_same(leafC->issuer,freshA) &&
          !grant_authority(s,st,leafC,principalC).generation);
    Client freshAClient={.fd=-1,.principal=freshA};
    failure(execute_client_wire(s,a,&freshAClient,delegate_grant_request(a,s,
        "new-a-cannot-use-old-root",root,accountB,RIGHT_READ,0,false)),"missing-space");

    check(compact(s));
    uint64_t oldBEpoch=oldB.epoch,freshBEpoch=freshB.epoch,freshAEpoch=freshA.epoch;
    release(s); initialize(s,directory); st=s->stores[0];
    check(current_principal(s,accountB).epoch==freshBEpoch &&
          current_principal(s,accountA).epoch==freshAEpoch &&
          s->principal_generation>=freshAEpoch);
    oldB=(Principal){.uid=accountB,.epoch=oldBEpoch};
    freshB=current_principal(s,accountB);
    check(find_decision(s,oldB,"principal-reused-id") &&
          find_decision(s,freshB,"principal-reused-id") &&
          find_receipt(s,oldB,"principal-reused-id") &&
          find_receipt(s,freshB,"principal-reused-id"));
}
static Atom *observed_request(Service *s,Arena *a,const char *id,bool add) {
    Atom *pattern=atom_var(a,"record");
    Atom *args[]={atom_int(a,s->stores[0]->id),form(a,"reads",&pattern,1),
        form(a,"writes",&pattern,1),atom_int(a,16),atom_int(a,60000)};
    Atom *observed=execute_wire(s,a,request(a,s,"observe-scope","observe",args,5));
    success(observed);
    Atom *ticket=part(observed,3),*row=atom_symbol(a,"saved"),*one=form(a,"add",&row,1);
    Atom *both[]={one,one};
    Atom *commit_args[]={atom_int(a,s->stores[0]->id),part(ticket,1),part(ticket,2),
        form(a,"ops",both,add ? 2 : 0)};
    return request(a,s,id,"batch-observed",commit_args,4);
}
static void scoped_publication(Service *s,Arena *a,const char *directory,unsigned fault) {
    success(execute_wire(s,a,open_request(a,s,"native-ordered")));
    if (!fault) {
        publication(s,a,observed_request(s,a,"scoped-noop",false));
        check(!s->observation_bytes && !s->snapshot_bytes);
        return;
    }
    Atom *req=observed_request(s,a,"scoped-uncertain",true);
    Bytes raw={0}; check(wire(req,&raw));
    off_t before=journal_size(s);
    reset_faults(); observed_journal=s->journal;
    if (fault<=2) write_fault=fault;
    else if (fault==3) fail_sync=true;
    else { fail_projection=true; allowed_projections=fault==5 ? 1 : 0; }
    check(!execute(s,a,req,raw)); check(s->poisoned && write_started);
    check(!s->observation_bytes && !s->snapshot_bytes);
    check(!find_receipt(s,root_principal(s),"scoped-uncertain"));
    reset_faults(); release(s); initialize(s,directory);
    bool complete=fault>=3;
    check(space_length64(s->stores[0]->space)==(complete ? 2u : 0u));
    check(s->stores[0]->revision==(complete ? 1u : 0u));
    Atom *again=execute(s,a,req,raw);
    if (complete) success(again);
    else {
        check(shape(again,"reply",4) && atom_is_symbol(part(again,2),"error"));
        check(atom_is_symbol(part(again,3),"observation-identity"));
        check(journal_size(s)==before);
        success(execute_wire(s,a,observed_request(s,a,"scoped-reprepared",true)));
    }
    check(space_length64(s->stores[0]->space)==2 && s->stores[0]->revision==1);
    bytes_free(&raw);
}
static void scope_deadline(Service *s,Arena *a) {
    check(execute_wire(s,a,open_request(a,s,"native-ordered"))!=NULL);
    Atom *patterns[32];
    for (size_t i=0;i<32;++i) patterns[i]=atom_symbol(a,"absent");
    Atom *args[]={atom_int(a,s->stores[0]->id),form(a,"reads",patterns,32),
        form(a,"writes",NULL,0),atom_int(a,1),atom_int(a,60000)};
    s->query_ms=20; fake_clock=1000;
    Atom *answer=execute_wire(s,a,request(a,s,"bounded-scopes","observe",args,5));
    uint64_t elapsed=fake_clock-1000; fake_clock=0;
    check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"error") &&
        atom_is_symbol(part(answer,3),"query-time-limit"));
    check(elapsed<40);
    check(s->observation_bytes==0 && s->snapshot_bytes==0);
}
static void scope_validation_encoding(Service *s,Arena *a) {
    check(execute_wire(s,a,open_request(a,s,"native-ordered"))!=NULL);
    Store *st=s->stores[0];
    for (int i=0;i<65;++i) {
        Atom *fields[]={atom_int(a,i),atom_int(a,i ? i+1 : 0)};
        check(store_add(st,form(a,"ScopeProbe",fields,2)));
    }
    Atom *variable=atom_var(a,"same");
    Atom *fields[]={variable,variable};
    Atom *pattern=form(a,"ScopeProbe",fields,2);
    Atom *args[]={atom_int(a,st->id),form(a,"reads",&pattern,1),
        form(a,"writes",NULL,0),atom_int(a,2),atom_int(a,60000)};
    Atom *answer=execute_wire(s,a,request(a,s,"capture-matching","observe",args,5));
    check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"ok"));
    Observation *o=find_observation(s,(uint64_t)part(part(answer,3),2)->ground.ival);
    check(o && o->views[0].rows.count==1);
    scope_row_encodings=0;
    check(scope_current(s,st,&o->views[0],now_ms()+5000)==NULL);
    check(scope_row_encodings==1);
    observation_free(s,o);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    decision_index_collision_control();
    principal_index_growth_control();
    grant_index_growth_control();
    Arena controls; arena_init_detached(&controls);
    grant_restore_structure_controls(&controls);
    account_origin_decision_controls(&controls);
    unbound_member_controls(&controls);
    legacy_v5_principal_migration_controls(&controls);
    principal_recovery_refusal_controls(&controls);
    arena_free(&controls);
    Service *s=calloc(1,sizeof(*s)); check(s!=NULL);
    const char *backends[]={"native","pathmap","mork"};
    for (unsigned trial=0;trial<26;++trial) {
        if (trial>0 && trial<3 && !cetta_mork_bridge_is_available()) continue;
        char directory[]="runtime/space-commit-XXXXXX"; check(mkdtemp(directory)!=NULL);
        Arena a; arena_init_detached(&a); initialize(s,directory);
        if (trial<3) prepared_publication(s,&a,backends[trial]);
        else if (trial<5) preparation_refusal(s,&a,trial==4);
        else if (trial<12) uncertain_publication(s,&a,directory,trial-4);
        else if (trial==12) account_publication(s,&a,directory);
        else if (trial==13) grant_generation_publication(s,&a,directory);
        else if (trial==14) explicit_read_grant_publication(s,&a);
        else if (trial==15) ticket_publication(s,&a,directory);
        else if (trial==16) attenuating_lineage_publication(s,&a,directory);
        else if (trial==17) stable_principal_publication(s,&a,directory);
        else if (trial<24) scoped_publication(s,&a,directory,trial-18);
        else if (trial==24) scope_deadline(s,&a);
        else scope_validation_encoding(s,&a);
        release(s); arena_free(&a);
        char path[sizeof(directory)+20];
        snprintf(path,sizeof(path),"%s/journal.csl",directory); check(!unlink(path));
        snprintf(path,sizeof(path),"%s/writer.lock",directory); check(!unlink(path));
        check(!rmdir(directory));
    }
    free(s); symbol_table_free(&symbols);
    printf("space publication and recovery: %u checks passed\n",checks);
    return 0;
}
