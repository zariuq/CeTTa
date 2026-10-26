/* Exercise the storage owner's real publication and recovery boundary. Faults
 * are injected at allocation and I/O interfaces, without production switches. */
#define main cetta_space_service_entry
#include "../../src/space_service_main.c"
#undef main

static unsigned checks, after_write_allocations, event_encodings;
static int observed_journal=-1;
static bool write_started, forbid_after_write, fail_sync, fail_projection, fail_event, fail_key;
static size_t fail_copy_size;
static unsigned write_fault, allowed_projections;

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
    if (write_started && fail_key &&
        (atom_is_symbol((Atom *)atom,"saved") || shape((Atom *)atom,"saved-open",2))) {
        fail_key=false; *data=NULL; *size=0; return ATOM_WIRE_NOMEM;
    }
    if (shape((Atom *)atom,"event-bytes",3) || shape((Atom *)atom,"event-from",4) ||
        shape((Atom *)atom,"event-observed-v1",5)) {
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
}
static void release(Service *s) {
    reset_faults();
    for (size_t i=0;i<STORES;++i) { store_free(s->stores[i]); s->stores[i]=NULL; }
    for (size_t i=0;i<SNAPSHOTS;++i) {
        snapshot_free(s,&s->snapshots[i]); observation_free(s,&s->observations[i]);
    }
    for (size_t i=0;i<s->receipt_count;++i) receipt_free(&s->receipts[i]);
    close(s->journal); close(s->lock); close(s->directory);
}
static void initialize(Service *s,const char *directory) {
    *s=(Service){.journal=-1,.directory=-1,.lock=-1,
        .max_facts=1000000,.max_live_bytes=64*1024*1024,
        .max_query_bytes=QUERY_BYTES,.max_snapshot_bytes=SNAPSHOT_BYTES,.query_ms=5000,.next_cursor=1,
        .process_uid=(uint32_t)geteuid(),.owner_uid=(uint32_t)geteuid()};
    check(directory_open(s,directory)); check(recover(s));
    check(random_id(s->boot));
}
static off_t journal_size(Service *s) {
    struct stat info; check(!fstat(s->journal,&info)); return info.st_size;
}
static Atom *request(Arena *a,Service *s,const char *id,const char *operation,Atom **args,size_t count) {
    Atom *parts[8]={atom_string(a,id),atom_symbol(a,operation),atom_string(a,s->uuid)};
    check(count<=5);
    for (size_t i=0;i<count;++i) parts[i+3]=args[i];
    return form(a,"request",parts,count+3);
}
static Atom *execute(Service *s,Arena *a,Atom *request,Bytes raw) {
    Client client={.fd=-1,.uid=s->process_uid,.owner=true}; return dispatch(s,&client,a,request,raw);
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
static void success(Atom *answer) { check(shape(answer,"reply",4) && atom_is_symbol(part(answer,2),"ok")); }

static void publication(Service *s,Arena *a,Atom *req) {
    Bytes raw={0}; check(wire(req,&raw));
    reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=execute(s,a,req,raw);
    check(write_started); check(!s->poisoned); success(answer);
    check(!after_write_allocations); check(event_encodings==1);
    reset_faults();
    const char *id=NULL; check(token(part(req,1),&id));
    Receipt *receipt=find_receipt(s,s->process_uid,id); check(receipt && same_bytes(receipt->request,raw));
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
    Receipt *kept=find_receipt(s,s->process_uid,"retained-0"); check(kept && same_bytes(kept->reply,oldest));
    check(!find_receipt(s,s->process_uid,"refused-preparation"));
    success(execute(s,a,req,raw));
    check(s->stores[0]->revision==revision+1 && space_length64(s->stores[0]->space)==1);
    check(!find_receipt(s,s->process_uid,"retained-0") && find_receipt(s,s->process_uid,"refused-preparation"));
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
    check(!find_receipt(s,s->process_uid,"uncertain")); check(!fail_key);
    if (fault==7) check(space_length64(s->stores[0]->space)==1 && !s->stores[0]->revision);
    check(!execute(s,a,req,raw));
    reset_faults(); release(s); initialize(s,directory);
    bool complete=fault>=3;
    uint64_t final_count=fault==7 ? 2u : fault>=5 ? 0u : 1u;
    check(space_length64(s->stores[0]->space)==(complete ? final_count : 0u));
    check(s->stores[0]->revision==initial_revision+(complete ? 1u : 0u));
    check((find_receipt(s,s->process_uid,"uncertain")!=NULL)==complete);
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
    Client member={.fd=-1,.uid=member_uid};
    reset_faults(); observed_journal=s->journal; forbid_after_write=true;
    Atom *answer=dispatch(s,&member,a,req,raw);
    check(write_started && !s->poisoned); success(answer);
    check(!after_write_allocations && event_encodings==1);
    reset_faults();
    check(find_receipt(s,s->process_uid,"open-store")!=NULL);
    check(find_receipt(s,member_uid,"open-store")!=NULL);
    Bytes response={0}; check(wire(answer,&response));
    release(s); initialize(s,directory);
    Receipt *receipt=find_receipt(s,member_uid,"open-store");
    check(receipt && same_bytes(receipt->reply,response));
    check(find_receipt(s,s->process_uid,"open-store")!=NULL);
    success(dispatch(s,&member,a,req,raw));
    check(s->stores[0]->revision==1);
    bytes_free(&raw); bytes_free(&response);
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
    check(!find_receipt(s,s->process_uid,"scoped-uncertain"));
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
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    Service *s=calloc(1,sizeof(*s)); check(s!=NULL);
    const char *backends[]={"native","pathmap","mork"};
    for (unsigned trial=0;trial<19;++trial) {
        if (trial>0 && trial<3 && !cetta_mork_bridge_is_available()) continue;
        char directory[]="runtime/space-commit-XXXXXX"; check(mkdtemp(directory)!=NULL);
        Arena a; arena_init_detached(&a); initialize(s,directory);
        if (trial<3) prepared_publication(s,&a,backends[trial]);
        else if (trial<5) preparation_refusal(s,&a,trial==4);
        else if (trial<12) uncertain_publication(s,&a,directory,trial-4);
        else if (trial==12) account_publication(s,&a,directory);
        else scoped_publication(s,&a,directory,trial-13);
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
