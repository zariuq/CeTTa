/* Inspect the retained descendant state in the actual implementation. */
#include "../src/native_space_agenda.c"
#include "parser.h"
#include "symbol.h"
#include "binding/frame_identity.h"
#include "native_cost.h"
#include "storage_census.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t checks;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);abort();}checks++;}while(0)
static Atom *parse(Arena *a,const char *text) {
    Atom **rows;int n=parse_metta_text(text,a,&rows);CHECK(n==1);
    Atom *row=rows[0];free(rows);return row;
}
static void add(Space *s,Arena *a,const char *text) {CHECK(space_admit_atom(s,a,parse(a,text)));}
static void run(CettaSpaceAgenda *a,unsigned expected) {
    unsigned committed=0u;
    for(unsigned i=0u;i<10000u;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        if(step==CETTA_SPACE_AGENDA_COMMITTED)committed++;
        if(step==CETTA_SPACE_AGENDA_CLOSED){CHECK(committed==expected);return;}
        if(step==CETTA_SPACE_AGENDA_PENDING) {
            fprintf(stderr,"pending while running\n");abort();
        }
    }
    CHECK(false);
}
static bool phase(CettaSpaceAgenda *a,const char *name) {
    Atom *state=cetta_space_agenda_pending(a);
    return atom_is_symbol_id(state->expr.elems[1]->expr.elems[1],symbol_intern_cstr(g_symbols,name));
}
static void until_phase(CettaSpaceAgenda *a,const char *name) {
    for(unsigned i=0u;i<10000u;i++) {
        if(phase(a,name))return;
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        CHECK(step!=CETTA_SPACE_AGENDA_CLOSED && step!=CETTA_SPACE_AGENDA_PENDING);
    }
    CHECK(false);
}
static unsigned until_pending(CettaSpaceAgenda *a) {
    unsigned committed=0u;
    for(unsigned i=0u;i<10000u;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        if(step==CETTA_SPACE_AGENDA_COMMITTED)committed++;
        if(step==CETTA_SPACE_AGENDA_PENDING)return committed;
        CHECK(step!=CETTA_SPACE_AGENDA_CLOSED);
    }
    CHECK(false);return committed;
}
static void mm2(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(p A)");add(&s,arena,"(p B)");add(&s,arena,"(q B)");
    add(&s,arena,"(exec (0 join) (, (p $x) (q $x)) (O (+ (hit $x)) (- (q $x))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){true,true,false,false});CHECK(a);
    run(a,1u);CHECK(space_contains_exact(&s,parse(arena,"(hit B)")));
    CHECK(!space_contains_exact(&s,parse(arena,"(q B)")));
    CHECK(space_contains_exact(&s,parse(arena,"(p A)")) && space_length64(&s)==3u);
    cetta_space_agenda_free(a);space_free(&s);
}
static void self_and_new_exec(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(exec (0 self) (, (exec $k $p $o)) (O (+ (seen $k)) (+ (exec (1 later) (,) (O (+ Done))))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){true,true,false,false});CHECK(a);
    run(a,2u);CHECK(space_contains_exact(&s,parse(arena,"(seen (0 self))")));
    CHECK(space_contains_exact(&s,parse(arena,"Done")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void self_reintroduced(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    const char *exec="(exec (0 self) (, (exec $key $in $out)) (O (+ Marked)))";
    add(&s,arena,exec);
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){true,true,false,false});CHECK(a);
    until_phase(a,"Capture");CHECK(space_length64(&s)==0u);
    Atom *reintroduced=parse(arena,exec);CHECK(space_admit_atom(&s,arena,reintroduced));
    for(unsigned i=0u;;i++) {
        CHECK(i<10000u);
        if(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_COMMITTED)break;
    }
    Atom *receipt=cetta_space_agenda_receipt(a);
    CHECK(receipt->expr.elems[2]->expr.elems[1]->ground.ival==1);
    CHECK(space_length64(&s)==2u && atom_alpha_eq(space_get_at64(&s,0u),reintroduced));
    run(a,1u);CHECK(space_length64(&s)==1u && space_contains_exact(&s,parse(arena,"Marked")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void scoped_alpha(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    uint32_t left_epoch=cetta_frame_identity_scope_fresh(&frames);
    uint32_t right_epoch=cetta_frame_identity_scope_fresh(&frames);
    uint32_t captured_epoch=cetta_frame_identity_scope_fresh(&frames);
    CHECK(left_epoch && right_epoch && captured_epoch);
    Atom *base=atom_var(arena,"$local");
    Atom *x=atom_var_like(arena,base,var_epoch_id(base->var_id,left_epoch));
    Atom *y=atom_var_like(arena,base,var_epoch_id(base->var_id,right_epoch));
    Atom *other=atom_var(arena,"$other");
    Atom *z=atom_var_like(arena,other,var_epoch_id(other->var_id,right_epoch));
    Atom *outer=atom_var_like(arena,base,var_epoch_id(base->var_id,captured_epoch));
    Atom *different_outer=atom_var_like(arena,other,var_epoch_id(other->var_id,captured_epoch));
    Atom *write=atom_symbol(arena,"write"),*read=atom_symbol(arena,"read");
    Atom *lw=atom_expr2(arena,write,x),*lr=atom_expr2(arena,read,x);
    Atom *rw=atom_expr2(arena,write,y),*rr=atom_expr2(arena,read,y);
    Atom *bad_read=atom_expr2(arena,read,z);
    CHECK(atom_alpha_eq(lw,rw) && atom_alpha_eq(lr,bad_read));
    Atom *left=atom_expr(arena,(Atom *[]){outer,lw,lr,atom_int(arena,7)},4u);
    Atom *right=atom_expr(arena,(Atom *[]){outer,rw,rr,atom_int(arena,7)},4u);
    Atom *bad=atom_expr(arena,(Atom *[]){outer,rw,bad_read,atom_int(arena,7)},4u);
    CHECK(atom_alpha_eq_scoped(left,right,&left_epoch,1u,&right_epoch,1u));
    CHECK(!atom_alpha_eq_scoped(left,bad,&left_epoch,1u,&right_epoch,1u));
    bad=atom_expr(arena,(Atom *[]){different_outer,rw,rr,atom_int(arena,7)},4u);
    CHECK(atom_alpha_eq(left,bad));
    CHECK(!atom_alpha_eq_scoped(left,bad,&left_epoch,1u,&right_epoch,1u));
    bad=atom_expr(arena,(Atom *[]){outer,rw,rr,atom_int(arena,8)},4u);
    CHECK(!atom_alpha_eq_scoped(left,bad,&left_epoch,1u,&right_epoch,1u));
    CHECK(!atom_alpha_eq_scoped(x,y,NULL,0u,NULL,0u));
}
static void captured_environment(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    Atom *exec=parse(arena,"(exec (0 outer) (I (read (token $outer))) (O (+ (kept $outer))))");
    Atom *outer=exec->expr.elems[2]->expr.elems[1]->expr.elems[1]->expr.elems[1];
    Bindings captured;CHECK(bindings_clone(&captured,env));
    CHECK(bindings_add_var(&captured,outer,atom_symbol(arena,"B")));
    add(&s,arena,"(token A)");add(&s,arena,"(token B)");CHECK(space_admit_atom(&s,arena,exec));
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,&captured,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    bindings_free(&captured);
    until_phase(a,"ValidateMatch");run(a,1u);
    CHECK(space_contains_exact(&s,parse(arena,"(kept B)")));
    CHECK(!space_contains_exact(&s,parse(arena,"(kept A)")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void alpha_set_members(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(edge $x $x)");add(&s,arena,"(edge $z $z)");
    add(&s,arena,"(edge $x $y)");
    add(&s,arena,"(exec 0 (,) (O (- (edge $a $a)) (+ Done)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){true,true,false,false});CHECK(a);
    run(a,1u);
    CHECK(space_length64(&s)==2u && space_contains_exact(&s,parse(arena,"Done")));
    Atom *remaining=space_get_at64(&s,0u);
    CHECK(remaining->kind==ATOM_EXPR && remaining->expr.len==3u);
    CHECK(remaining->expr.elems[1]->var_id!=remaining->expr.elems[2]->var_id);
    cetta_space_agenda_free(a);space_free(&s);
}
static void transaction(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(token A)");add(&s,arena,"(token A)");add(&s,arena,"(fact B)");
    add(&s,arena,"(exec (0 consume) (I (take (token $x)) (all (fact $v) $rows) (absent Stop)) (O (+ (result $x $rows))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    run(a,1u);CHECK(space_contains_exact(&s,parse(arena,"(result A ((fact B)))")));
    CHECK(space_contains_exact(&s,parse(arena,"(token A)")) && space_length64(&s)==3u);
    cetta_space_agenda_free(a);space_free(&s);
}
static void disabled_then_enabled(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(exec (0 waiting) (I (take Wake)) (O (+ Awake)))");
    add(&s,arena,"(exec (1 ready) (I) (O (+ Passed)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    CHECK(until_pending(a)==1u);CHECK(space_contains_exact(&s,parse(arena,"Passed")));
    CHECK(!space_contains_exact(&s,parse(arena,"Awake")));
    add(&s,arena,"Wake");run(a,1u);
    CHECK(space_contains_exact(&s,parse(arena,"Awake")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void shared_reads_and_takes(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);add(&s,arena,"Token");
    add(&s,arena,"(exec (0 read) (I (read Token) (read Token)) (O (+ Read)))");
    add(&s,arena,"(exec (1 blocked) (I (take Token) (take Token)) (O (+ Overdraw)))");
    add(&s,arena,"(exec (2 take) (I (take Token)) (O (+ Taken)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    CHECK(until_pending(a)==2u);
    CHECK(space_contains_exact(&s,parse(arena,"Read")) && space_contains_exact(&s,parse(arena,"Taken")));
    CHECK(!space_contains_exact(&s,parse(arena,"Token")) && !space_contains_exact(&s,parse(arena,"Overdraw")));
    add(&s,arena,"Token");add(&s,arena,"Token");run(a,1u);
    CHECK(space_contains_exact(&s,parse(arena,"Overdraw")) && !space_contains_exact(&s,parse(arena,"Token")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void phantoms(Arena *arena,Bindings *env) {
    const char *phases[]={"Capture","Match","ValidateCapture","ValidateMatch","Build","Sink","Commit"};
    for(size_t i=0u;i<sizeof(phases)/sizeof(*phases);i++) {
        Space s={0};space_init(&s);add(&s,arena,"(fact A)");
        add(&s,arena,"(exec (0 all) (I (all (fact $v) $rows)) (O (+ (result $rows))))");
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
        until_phase(a,phases[i]);add(&s,arena,"(fact B)");run(a,1u);
        CHECK(space_contains_exact(&s,parse(arena,"(result ((fact A) (fact B)))")));
        CHECK(!space_contains_exact(&s,parse(arena,"(result ((fact A)))")));
        cetta_space_agenda_free(a);space_free(&s);
    }
    Space s={0};space_init(&s);
    add(&s,arena,"(exec (0 absence) (I (absent Stop)) (O (+ Invalid)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    until_phase(a,"Commit");add(&s,arena,"Stop");CHECK(until_pending(a)==0u);
    CHECK(!space_contains_exact(&s,parse(arena,"Invalid")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void replaced_occurrence(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);add(&s,arena,"Token");
    add(&s,arena,"(exec (0 take) (I (take Token)) (O (+ Wrong)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    until_phase(a,"Commit");CHECK(space_remove(&s,parse(arena,"Token")));add(&s,arena,"Token");
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
    Atom *state=cetta_space_agenda_pending(a);
    CHECK(atom_is_symbol_id(state->expr.elems[5]->expr.elems[1],symbol_intern_cstr(g_symbols,"StaleSelectedOccurrence")));
    CHECK(space_contains_exact(&s,parse(arena,"Token")) && !space_contains_exact(&s,parse(arena,"Wrong")));
    cetta_space_agenda_free(a);space_free(&s);
}
static void world_fork_ownership(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"Token");add(&s,arena,"Token");
    add(&s,arena,"(exec Take (I (take Token)) (O (+ Taken)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){false,false,true,true});CHECK(a);
    for(unsigned i=0u;;i++) {
        CHECK(i<1000u);
        if(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_FORK)break;
    }
    CettaSpaceAgenda *child=cetta_space_agenda_child(a);CHECK(child);
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_FORK);
    CHECK(cetta_space_agenda_child(a)==child);
    CHECK(!cetta_space_agenda_accept_child(a,a));
    CHECK(cetta_space_agenda_child(a)==child);
    CHECK(space_length64(&s)==3u);
    CHECK(cetta_space_agenda_accept_child(a,child));
    CHECK(!cetta_space_agenda_accept_child(a,child));
    CHECK(!cetta_space_agenda_child(a));
    add(&s,arena,"Phantom");
    CHECK(!cetta_space_agenda_current(child));
    CHECK(cetta_space_agenda_step(child)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(!space_contains_exact(&s,parse(arena,"Taken")));
    /* Cancelling before either the parent matcher or child commit finishes
     * releases both owned continuations without touching live storage. */
    cetta_space_agenda_free(a);CHECK(space_length64(&s)==4u);space_free(&s);
}

static void prepared_match_ownership(Arena *arena,Bindings *env) {
    Space s={0};space_init(&s);
    add(&s,arena,"(fact A)");
    add(&s,arena,"(exec Collect (I (all (fact $x) $rows)) (O (+ (Result $rows))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    CettaSpaceAgendaMatch *match=NULL;
    for(unsigned i=0u;!match;i++) {
        CHECK(i<1000u);
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
        match=cetta_space_agenda_match_capture(a);
    }
    CHECK(cetta_space_agenda_match_capture(a)==match);
    CHECK(cetta_space_agenda_match_run(match,CETTA_SPACE_AGENDA_MATCH_NODES));
    CHECK(!cetta_space_agenda_match_run(match,CETTA_SPACE_AGENDA_MATCH_NODES));
    CHECK(!cetta_space_agenda_match_capture(a));
    /* A completed private matcher grants no commit authority. Revalidation
     * must discover an insertion made before its result is installed. */
    add(&s,arena,"(fact B)");
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
    run(a,1u);
    CHECK(space_contains_exact(&s,parse(arena,"(Result ((fact A) (fact B)))")));
    CHECK(!space_contains_exact(&s,parse(arena,"(Result ((fact A)))")));
    cetta_space_agenda_free(a);space_free(&s);

    space_init(&s);add(&s,arena,"Token");
    add(&s,arena,"(exec Take (I (take Token)) (O (+ Taken)))");
    a=cetta_space_agenda_new(&s,arena,env,(CettaSpaceAgendaContract){false,false,true,true});CHECK(a);
    match=NULL;
    for(unsigned i=0u;!match;i++) {
        CHECK(i<1000u);
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
        match=cetta_space_agenda_match_capture(a);
    }
    CHECK(cetta_space_agenda_match_run(match,CETTA_SPACE_AGENDA_MATCH_NODES));add(&s,arena,"Changed");
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(space_contains_exact(&s,parse(arena,"Token")) && space_length64(&s)==3u);
    /* Cancellation also owns a completed but unpublished private result. */
    cetta_space_agenda_free(a);space_free(&s);
}

static Atom *wide_row(Arena *arena,Atom *leaf) {
    Atom *items[98];items[0]=atom_symbol(arena,"Wide");
    for(size_t i=1u;i<98u;i++)items[i]=leaf;
    return atom_expr(arena,items,98u);
}

static void partial_match_authority(Arena *arena,Bindings *env) {
    for(unsigned install=0u;install<2u;install++) {
        Space s={0};space_init(&s);
        Atom *row=wide_row(arena,atom_symbol(arena,"A"));
        CHECK(space_admit_atom(&s,arena,row));
        Atom *pattern=wide_row(arena,atom_var(arena,"$x"));
        Atom *inputs=atom_expr2(arena,atom_symbol(arena,"I"),
            atom_expr2(arena,atom_symbol(arena,"read"),pattern));
        Atom *outputs=parse(arena,"(O (+ NeverPublished))");
        CHECK(space_admit_atom(&s,arena,atom_expr(arena,(Atom *[]){
            atom_symbol(arena,"exec"),atom_int(arena,0),inputs,outputs},4u)));
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
            (CettaSpaceAgendaContract){false,false,true,true});CHECK(a);
        CettaSpaceAgendaMatch *match=NULL;
        for(unsigned i=0u;!match;i++) {
            CHECK(i<1000u);
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
            match=cetta_space_agenda_match_capture(a);
        }
        CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
        CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,37u);
        CHECK(cetta_space_agenda_match_run(match,7u));
        CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==7u);
        if(install) {
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(cetta_space_agenda_match_capture(a)==match);
        }
        CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==7u);
        CettaStorageCensus census={0};cetta_space_agenda_storage(a,&census);
        cetta_storage_finish(&census);
        CHECK(!census.incomplete && !census.overflow && census.bytes>0u);
        cetta_storage_census_free(&census);
        add(&s,arena,"Changed");
        CHECK(!cetta_space_agenda_current(a));
        for(unsigned i=0u;i<2u;i++) {
            cetta_space_agenda_resume(a);
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==7u);
        }
        CHECK(space_length64(&s)==3u && space_contains_exact(&s,row));
        CHECK(!space_contains_exact(&s,parse(arena,"NeverPublished")));
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&meter);
        /* Both an uninstalled packet and its installed partial worklist
         * remain owned by the cancelled agenda. No live write is committed. */
        cetta_space_agenda_free(a);space_free(&s);
    }
    puts("PASS: partial resource matcher storage, invalidation and cancellation retain paid work before and after installation");
}

static CettaNativeCostSnapshot retained_match_run(Arena *arena,Bindings *env,
        uint64_t grant) {
    Space s={0};space_init(&s);
    Atom *row=wide_row(arena,atom_symbol(arena,"A"));
    CHECK(space_admit_atom(&s,arena,row));CHECK(space_admit_atom(&s,arena,row));
    Atom *pattern=wide_row(arena,atom_var(arena,"$x"));
    Atom *collection=atom_expr(arena,(Atom *[]){pattern,pattern},2u);
    Atom *input=atom_expr(arena,(Atom *[]){atom_symbol(arena,"all"),pattern,collection},3u);
    Atom *inputs=atom_expr2(arena,atom_symbol(arena,"I"),input);
    Atom *output=atom_expr2(arena,atom_symbol(arena,"Kept"),atom_var(arena,"$x"));
    Atom *sinks=atom_expr2(arena,atom_symbol(arena,"O"),
        atom_expr2(arena,atom_symbol(arena,"+"),output));
    CHECK(space_admit_atom(&s,arena,atom_expr(arena,(Atom *[]){
        atom_symbol(arena,"exec"),atom_int(arena,0),inputs,sinks},4u)));
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){false,false,true,false});CHECK(a);
    CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
    CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,31u);
    unsigned committed=0u,private_steps=0u,short_steps=0u;
    bool closed=false;
    for(unsigned i=0u;i<10000u;i++) {
        CettaSpaceAgendaMatch *match=grant?cetta_space_agenda_match_capture(a):NULL;
        CettaSpaceAgendaStep step;
        uint64_t nodes=meter.total.counts[CETTA_COST_MATCH_NODE];
        if(match) {
            uint64_t work=meter.total.work;
            CHECK(!cetta_space_agenda_match_run(match,0u));
            CHECK(meter.total.work==work);
            CHECK(cetta_space_agenda_match_run(match,grant));
            uint64_t used=meter.total.counts[CETTA_COST_MATCH_NODE]-nodes;
            CHECK(used>0u && used<=grant);
            private_steps++;if(used<grant)short_steps++;
            CHECK(!cetta_space_agenda_match_run(match,grant));
            CHECK(!cetta_space_agenda_match_capture(a));
            nodes=meter.total.counts[CETTA_COST_MATCH_NODE];
            /* Resumption keeps the paid packet. Installing it must not
             * advance or restart the canonical matcher. */
            cetta_space_agenda_resume(a);
            step=cetta_space_agenda_step(a);
            CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==nodes);
        } else {
            step=cetta_space_agenda_step(a);
            CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]-nodes<=
                CETTA_SPACE_AGENDA_MATCH_NODES);
        }
        CHECK(step!=CETTA_SPACE_AGENDA_PENDING);
        if(step==CETTA_SPACE_AGENDA_COMMITTED)committed++;
        if(step==CETTA_SPACE_AGENDA_CLOSED){closed=true;break;}
    }
    CHECK(closed && committed==1u);
    CHECK(space_contains_exact(&s,parse(arena,"(Kept A)")));
    CHECK(space_length64(&s)==3u);
    /* This small catalogue uses the ordinary full physical scan: two Wide
     * rows, one refuted exec row and one collection match, independently
     * replayed for validation. Grants cannot create fresh activations. */
    if(meter.total.counts[CETTA_COST_MATCH_ATTEMPT]!=8u)
        fprintf(stderr,"retained resource grant %llu: %llu attempts, %llu nodes\n",
            (unsigned long long)grant,
            (unsigned long long)meter.total.counts[CETTA_COST_MATCH_ATTEMPT],
            (unsigned long long)meter.total.counts[CETTA_COST_MATCH_NODE]);
    CHECK(meter.total.counts[CETTA_COST_MATCH_ATTEMPT]==8u);
    if(grant){CHECK(private_steps>8u);if(grant>1u)CHECK(short_steps>0u);}
    cetta_native_cost_scope_leave(&scope);
    CettaNativeCostSnapshot total=meter.total;cetta_native_cost_free(&meter);
    cetta_space_agenda_free(a);space_free(&s);
    return total;
}

static void retained_match_quanta(Arena *arena,Bindings *env) {
    CettaNativeCostSnapshot serial=retained_match_run(arena,env,0u);
    CettaNativeCostSnapshot small=retained_match_run(arena,env,7u);
    CettaNativeCostSnapshot normal=retained_match_run(arena,env,CETTA_SPACE_AGENDA_MATCH_NODES);
    CHECK(serial.counts[CETTA_COST_MATCH_NODE]==small.counts[CETTA_COST_MATCH_NODE]);
    CHECK(serial.counts[CETTA_COST_MATCH_NODE]==normal.counts[CETTA_COST_MATCH_NODE]);
    puts("PASS: retained resource node grants preserve duplicate rows, collection binding and eight exact activations; joins install paid work once");
}

static void *schedule_advice(void *context,Atom *exec,const Bindings *bindings,Atom *authored) {
    (void)context;(void)bindings;(void)authored;return exec;
}
static bool schedule_before(void *context,const void *left,const void *right) {
    (void)context;const Atom *a=left,*b=right;
    return a->expr.elems[1]->ground.ival<b->expr.elems[1]->ground.ival;
}
static void retained_schedule(Arena *arena,Bindings *env) {
    Space space;space_init(&space);
    add(&space,arena,"Token");
    add(&space,arena,"(exec 9 (I (read Token)) (O (+ Nine)))");
    add(&space,arena,"(exec 1 (I (read Token)) (O (+ One)))");
    add(&space,arena,"(exec 5 (I (read Token)) (O (+ Five)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true,.snapshot_worlds=true});CHECK(a);
    CettaSelectionAutomaton schedule;CHECK(cetta_selection_automaton_ratio(2u,&schedule));
    CHECK(cetta_space_agenda_control(a,&schedule,schedule_advice,schedule_before,NULL));
    bool fork=false;
    for(size_t i=0u;i<1000u && !fork;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        if(step==CETTA_SPACE_AGENDA_FORK) {
            CettaSpaceAgenda *child=cetta_space_agenda_child(a);CHECK(child);
            CHECK(cetta_space_agenda_accept_child(a,child));fork=true;
        }
        CHECK(step!=CETTA_SPACE_AGENDA_PENDING && step!=CETTA_SPACE_AGENDA_CLOSED);
    }
    CHECK(fork && a->owned_next && a->schedule.state==1u && a->owned_next->schedule.state==1u);
    const CettaSpaceAgenda root=*a,child=*a->owned_next;
    for(unsigned repeat=0u;repeat<3u;repeat++) {
        CHECK(cetta_space_agenda_control(a,&schedule,schedule_advice,schedule_before,NULL));
        CHECK(!memcmp(&root,a,sizeof(root)));
        CHECK(!memcmp(&child,a->owned_next,sizeof(child)));
    }
    CHECK(cetta_selection_automaton_ratio(4u,&schedule));
    CHECK(cetta_space_agenda_control(a,&schedule,schedule_advice,schedule_before,NULL));
    CHECK(!a->schedule.state && a->schedule.state_count==5u);
    CHECK(!a->owned_next->schedule.state && a->owned_next->schedule.state_count==5u);
    cetta_space_agenda_free(a);space_free(&space);
    puts("PASS: unchanged resource schedule retains each descendant phase; graph changes reset explicitly");
}

int main(void) {
    SymbolTable symbols;VarInternTable vars;Arena arena;Bindings env;
    symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    var_intern_init(&vars);g_var_intern=&vars;g_hashcons=NULL;arena_init(&arena);bindings_init(&env);
    scoped_alpha(&arena);captured_environment(&arena,&env);alpha_set_members(&arena,&env);
    mm2(&arena,&env);self_and_new_exec(&arena,&env);self_reintroduced(&arena,&env);transaction(&arena,&env);
    disabled_then_enabled(&arena,&env);shared_reads_and_takes(&arena,&env);
    phantoms(&arena,&env);replaced_occurrence(&arena,&env);
    world_fork_ownership(&arena,&env);
    prepared_match_ownership(&arena,&env);
    partial_match_authority(&arena,&env);
    retained_match_quanta(&arena,&env);
    retained_schedule(&arena,&env);
    bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
    puts("PASS: native space agenda joins, restores self reads, creates execs and consumes physical bag occurrences");
    printf("%zu checks passed\n",checks);return 0;
}
