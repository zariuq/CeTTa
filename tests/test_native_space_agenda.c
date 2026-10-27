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

typedef struct { unsigned calls; bool allow, mutate; Arena *arena; } ActivationGateTest;
static bool activation_test_permit(void *raw,CettaSpaceAgenda *agenda) {
    ActivationGateTest *gate=raw;gate->calls++;
    CHECK(cetta_space_agenda_activation_source(agenda));
    CHECK(phase(agenda,"Activate"));
    if(gate->mutate && gate->calls==1u)add(agenda->space,gate->arena,"Injected");
    return gate->allow;
}
static void controlled_mm2_activation(Arena *arena,Bindings *env) {
    const char *exec="(exec 0 (, Permit) (O (+ Won) (- Permit)))";
    for(unsigned variant=0u;variant<4u;variant++) {
        Space s={0};space_init(&s);add(&s,arena,"Permit");add(&s,arena,exec);
        add(&s,arena,exec);
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
            (CettaSpaceAgendaContract){.set_support=true,.raw_mm2_order=true});CHECK(a);
        SpaceReadToken before=space_read_token(&s);
        ActivationGateTest gate={.allow=variant==1u || variant==3u,
            .mutate=variant==3u,.arena=arena};
        CettaSpaceAgendaActivationScope scope={0};
        CHECK(cetta_space_agenda_activation_enter(&scope,a,activation_test_permit,&gate));
        CHECK(agenda_operations==&agenda_controlled_plain_operations);
        if(variant==1u) {
            run(a,1u);CHECK(gate.calls==1u);
        } else {
            CHECK(until_pending(a)==0u);CHECK(gate.calls==1u);
            CHECK(space_contains_exact(&s,parse(arena,exec)));
            CHECK(!space_contains_exact(&s,parse(arena,"Won")));
            CHECK(space_read_token_is_current(before)==!gate.mutate);
            if(variant==0u) {
                CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
                CHECK(gate.calls==2u && space_read_token_is_current(before));
                /* An inner Transaction uses the same phase implementation,
                 * without acquiring or invoking the outer MM2 gate. */
                Space inner={0};space_init(&inner);
                add(&inner,arena,"(exec Inner (I) (O (+ InnerDone)))");
                CettaSpaceAgenda *other=cetta_space_agenda_new(&inner,arena,env,
                    (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(other);
                run(other,1u);CHECK(gate.calls==2u);
                cetta_space_agenda_free(other);space_free(&inner);
            } else if(variant==2u)add(&s,arena,"Unrelated");
        }
        cetta_space_agenda_activation_leave(&scope);
        CHECK(agenda_operations==&agenda_plain_operations);
        if(variant==0u)run(a,1u);
        if(variant<2u) {
            CHECK(space_length64(&s)==1u);
            CHECK(space_contains_exact(&s,parse(arena,"Won")));
        } else {
            SpaceReadToken held=space_read_token(&s);
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(a->pending && !strcmp(a->pending,"StaleActivationSource"));
            CHECK(space_read_token_is_current(held));
            CHECK(space_contains_exact(&s,parse(arena,exec)));
            CHECK(!space_contains_exact(&s,parse(arena,"Won")));
        }
        cetta_space_agenda_free(a);space_free(&s);
    }
    puts("PASS: optional MM2 activation gates retain directives before removal, resume once and refuse changed sources without affecting inner transactions");
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
    CettaFrameIdentity left_epoch=cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity right_epoch=cetta_frame_identity_scope_fresh(&frames);
    CettaFrameIdentity captured_epoch=cetta_frame_identity_scope_fresh(&frames);
    CHECK(left_epoch.word && right_epoch.word && captured_epoch.word);
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
    CHECK(!var_id_eq(remaining->expr.elems[1]->var_id, remaining->expr.elems[2]->var_id));
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
    CHECK(!cetta_space_agenda_publication_source(child));
    SpaceReadToken before=space_read_token(&s);
    until_phase(child,"Commit");
    Atom *selected=cetta_space_agenda_publication_source(child);CHECK(selected);
    CHECK(selected==cetta_space_agenda_publication_source(child));
    CHECK(phase(child,"Commit") && space_read_token_is_current(before));
    CHECK(space_length64(&s)==3u && !space_contains_exact(&s,parse(arena,"Taken")));
    add(&s,arena,"Phantom");
    CHECK(!cetta_space_agenda_current(child));
    CHECK(!cetta_space_agenda_publication_source(child));
    CHECK(cetta_space_agenda_step(child)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(!space_contains_exact(&s,parse(arena,"Taken")));
    /* Cancelling before either the parent matcher or child commit finishes
     * releases both owned continuations without touching live storage. */
    cetta_space_agenda_free(a);CHECK(space_length64(&s)==4u);space_free(&s);

    /* A later private world can be current even though its original source
     * has changed. Inspection must check both dependencies. */
    Space origin={0};space_init(&origin);add(&origin,arena,"Token");
    add(&origin,arena,"(exec First (I (take Token)) (O (+ Ready) (+ (exec Next (I (read Ready)) (O (+ Done))))))");
    a=cetta_space_agenda_new(&origin,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true,.snapshot_worlds=true});CHECK(a);
    for(unsigned i=0u;;i++) {
        CHECK(i<1000u);
        if(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_FORK)break;
    }
    child=cetta_space_agenda_child(a);CHECK(child);
    CHECK(cetta_space_agenda_accept_child(a,child));
    until_phase(child,"Commit");
    CHECK(cetta_space_agenda_step(child)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(child->space==&child->world && child->space!=&origin);
    for(unsigned i=0u;;i++) {
        CHECK(i<1000u);
        if(cetta_space_agenda_step(child)==CETTA_SPACE_AGENDA_FORK)break;
    }
    CettaSpaceAgenda *later=cetta_space_agenda_child(child);CHECK(later);
    CHECK(cetta_space_agenda_accept_child(child,later));
    until_phase(later,"Commit");CHECK(cetta_space_agenda_publication_source(later));
    SpaceReadToken private_read=space_read_token(child->space);
    add(&origin,arena,"Phantom");
    CHECK(selected_occurrence_current(later) && space_read_token_is_current(later->capture_read));
    CHECK(!cetta_space_agenda_current(later) && !cetta_space_agenda_publication_source(later));
    CHECK(cetta_space_agenda_step(later)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(space_read_token_is_current(private_read));
    CHECK(!space_contains_exact(child->space,parse(arena,"Done")));
    cetta_space_agenda_free(a);space_free(&origin);
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
    CHECK(a->borrowed_capture);
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_RETRY);
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
        /* A grant counts matcher steps. Entering a pair is a node; following
         * a bound variable (each repeated $x after the first) is a step that
         * is not a node, as a whole alias chain is one node. The seven steps
         * leave the 98-field row unfinished and pay at most seven nodes. */
        const uint64_t paid=meter.total.counts[CETTA_COST_MATCH_NODE];
        CHECK(match->decision==CETTA_MATCH_CURSOR_PROGRESS);
        CHECK(paid>0u && paid<=7u);
        if(install) {
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(cetta_space_agenda_match_capture(a)==match);
        }
        CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==paid);
        CettaStorageCensus census={0};cetta_space_agenda_storage(a,&census);
        cetta_storage_finish(&census);
        CHECK(!census.incomplete && !census.overflow && census.bytes>0u);
        cetta_storage_census_free(&census);
        add(&s,arena,"Changed");
        CHECK(!cetta_space_agenda_current(a));
        for(unsigned i=0u;i<2u;i++) {
            cetta_space_agenda_resume(a);
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==paid);
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

static size_t discover_choices(CettaSpaceAgendaDiscovery *d,
        CettaSpaceAgendaChoice *views,size_t capacity) {
    size_t count=0u;
    for(unsigned i=0u;i<100000u;i++) {
        CettaSpaceAgendaChoice view;
        CettaSpaceAgendaDiscoveryStep step=cetta_space_agenda_discovery_step(d,&view);
        CHECK(cetta_space_agenda_discovery_current(d));
        if(step==CETTA_SPACE_DISCOVERY_EXHAUSTED)return count;
        if(step==CETTA_SPACE_DISCOVERY_CHOICE) {
            CHECK(count<capacity && view.current);
            views[count++]=view;
        } else {
            if(step!=CETTA_SPACE_DISCOVERY_PROGRESS)
                fprintf(stderr,"discovery: %s\n",cetta_space_agenda_discovery_pending(d));
            CHECK(step==CETTA_SPACE_DISCOVERY_PROGRESS);
        }
    }
    CHECK(false);return count;
}

static CettaSpaceAgendaStep perform_selection(CettaSpaceAgendaSelection *s) {
    for(unsigned i=0u;i<100000u;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_selection_step(s);
        if(step==CETTA_SPACE_AGENDA_COMMITTED || step==CETTA_SPACE_AGENDA_PENDING)
            return step;
        CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
    }
    CHECK(false);return CETTA_SPACE_AGENDA_PENDING;
}

static void raw_activation_discovery(Arena *arena,Bindings *env) {
    const char *chosen="(exec 9 (, Permit) (O (+ Beta) (- Permit)))";
    for(unsigned variant=0u;variant<4u;variant++) {
        Space space={0};space_init(&space);add(&space,arena,"Permit");
        add(&space,arena,"(exec 0 (, Permit) (O (+ Alpha) (- Permit)))");
        add(&space,arena,chosen);
        add(&space,arena,"(exec 10 (, Missing) (O (+ Wrong)))");
        SpaceReadToken before=space_read_token(&space);
        CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new_owned_kind(
            &space,env,CETTA_SPACE_CHOICE_MM2_ACTIVATION);
        CHECK(d && !d->cursor_ready && !d->count && !d->preparing);
        CHECK(!cetta_space_agenda_discovery_new_owned_kind(&space,env,(CettaSpaceAgendaChoiceKind)99));
        CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
        CettaNativeCostScope measured;cetta_native_cost_scope_enter(&measured,&meter,91u);
        CettaSpaceAgendaChoice views[3];CHECK(discover_choices(d,views,3u)==3u);
        CHECK(meter.total.counts[CETTA_COST_MATCH_ATTEMPT]==0u &&
              meter.total.counts[CETTA_COST_MATCH_NODE]==0u);
        cetta_native_cost_scope_leave(&measured);cetta_native_cost_free(&meter);
        CHECK(space_read_token_is_current(before));
        for(size_t i=0u;i<3u;i++) {
            CHECK(views[i].kind==CETTA_SPACE_CHOICE_MM2_ACTIVATION &&
                views[i].source_index==i+1u && !views[i].outputs && !views[i].dependencies);
            CHECK(d->ready[i]->phase==AGENDA_ACTIVATE && !d->ready[i]->borrowed_capture &&
                !d->ready[i]->snapshot_ready && !d->ready[i]->row_count);
        }
        CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,views[1].ticket);
        CHECK(selected && !cetta_space_agenda_discovery_take(d,views[1].ticket));
        cetta_space_agenda_discovery_free(d);
        CettaSpaceAgenda *provider=cetta_space_agenda_selection_provider(selected);
        if(variant==1u)add(&space,arena,"Unrelated");
        if(variant==2u) {CHECK(space_remove(&space,parse(arena,chosen)));add(&space,arena,chosen);}
        if(variant==3u) {
            cetta_space_agenda_selection_free(selected);
            CHECK(space_read_token_is_current(before));
            CHECK(space_contains_exact(&space,parse(arena,chosen)));
            space_free(&space);continue;
        }
        CettaSpaceAgendaSelectionScope scope={0};CHECK(cetta_space_agenda_selection_enter(&scope,selected));
        if(variant) {
            SpaceReadToken unchanged=space_read_token(&space);
            CHECK(cetta_space_agenda_step(provider)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(!strcmp(cetta_space_agenda_selection_pending(selected),"StaleActivationSource"));
            CHECK(space_read_token_is_current(unchanged) && !selected->completed);
            CHECK(space_contains_exact(&space,parse(arena,chosen)));
            cetta_space_agenda_resume(provider);
            CHECK(cetta_space_agenda_step(provider)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(!strcmp(provider->pending,"StaleActivationSource"));
            CHECK(space_read_token_is_current(unchanged));
        } else {
            ActivationGateTest gate={.arena=arena};CettaSpaceAgendaActivationScope activation={0};
            CHECK(cetta_space_agenda_activation_enter(&activation,provider,activation_test_permit,&gate));
            CHECK(cetta_space_agenda_step(provider)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(cetta_space_agenda_step(provider)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(gate.calls==2u && !selected->completed && space_read_token_is_current(before));
            cetta_space_agenda_activation_leave(&activation);
            CHECK(cetta_space_agenda_step(provider)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(selected->completed && provider->phase==AGENDA_CAPTURE);
            CHECK(!space_contains_exact(&space,parse(arena,chosen)));
            CHECK(space_contains_exact(&space,parse(arena,"Permit")));
            CHECK(!space_contains_exact(&space,parse(arena,"Beta")));
            run(provider,3u);
            CHECK(space_length64(&space)==1u && space_contains_exact(&space,parse(arena,"Beta")));
        }
        cetta_space_agenda_selection_leave(&scope);
        CHECK(agenda_operations==&agenda_plain_operations);
        if(!variant) {
            SpaceReadToken complete=space_read_token(&space);
            CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
            CHECK(space_read_token_is_current(complete));
        }
        cetta_space_agenda_selection_free(selected);space_free(&space);
    }
    CHECK(!cetta_space_agenda_discovery_new_owned_kind(NULL,env,CETTA_SPACE_CHOICE_MM2_ACTIVATION));
    puts("PASS: raw MM2 discovery performs no matching or mutation; chosen activation is owned, exact-source guarded and separate from subsequent matching");
}

static size_t count_exact(Space *space,Arena *arena,const char *text) {
    Atom *wanted=parse(arena,text);size_t count=0u;
    for(CettaIndex i=0u;i<space_length64(space);i++)
        if(atom_eq(wanted,space_get_at64(space,i)))count++;
    return count;
}

static void continuing_request_reply(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);add(&space,arena,"Slot");
    add(&space,arena,"(exec Server (I (take Slot) (take (Request $id $payload)) "
        "(read (exec Server $input $output))) "
        "(O (+ (Reply $id $payload)) (+ (exec Server $input $output))))");
    add(&space,arena,"(exec Consumer (I (take (Reply $id $payload)) (take Acknowledge) "
        "(read (exec Consumer $input $output))) "
        "(O (+ Slot) (+ (Delivered $id $payload)) (+ (exec Consumer $input $output))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    CHECK(until_pending(a)==0u && !strcmp(a->pending,"WaitingForMatch"));
    add(&space,arena,"(Request Equal Same)");add(&space,arena,"(Request Equal Same)");
    cetta_space_agenda_resume(a);CHECK(until_pending(a)==1u);
    CHECK(count_exact(&space,arena,"(Request Equal Same)")==1u);
    CHECK(count_exact(&space,arena,"(Reply Equal Same)")==1u);
    CHECK(count_exact(&space,arena,"(Delivered Equal Same)")==0u);
    CHECK(count_exact(&space,arena,"Slot")==0u);
    SpaceReadToken waiting=space_read_token(&space);
    cetta_space_agenda_resume(a);CHECK(until_pending(a)==0u);
    CHECK(space_read_token_is_current(waiting));
    add(&space,arena,"Acknowledge");cetta_space_agenda_resume(a);
    CHECK(until_pending(a)==2u);
    CHECK(count_exact(&space,arena,"(Request Equal Same)")==0u);
    CHECK(count_exact(&space,arena,"(Reply Equal Same)")==1u);
    CHECK(count_exact(&space,arena,"(Delivered Equal Same)")==1u);
    CHECK(count_exact(&space,arena,"Slot")==0u);
    add(&space,arena,"(Request Later Same)");add(&space,arena,"Acknowledge");
    cetta_space_agenda_resume(a);CHECK(until_pending(a)==2u);
    CHECK(count_exact(&space,arena,"(Delivered Equal Same)")==2u);
    CHECK(count_exact(&space,arena,"(Delivered Later Same)")==0u);
    CHECK(count_exact(&space,arena,"(Reply Later Same)")==1u);
    add(&space,arena,"Acknowledge");cetta_space_agenda_resume(a);
    CHECK(until_pending(a)==1u && a->commits==6u);
    CHECK(count_exact(&space,arena,"(Delivered Later Same)")==1u);
    CHECK(count_exact(&space,arena,"Slot")==1u);
    CHECK(count_exact(&space,arena,"Acknowledge")==0u);
    CHECK(space_length64(&space)==6u);
    waiting=space_read_token(&space);
    cetta_space_agenda_free(a);
    CHECK(space_read_token_is_current(waiting));space_free(&space);
    puts("PASS: one retained request/reply provider preserves equal occurrences, bounded output capacity, acknowledgments and late arrivals");
}

static void borrowed_discovery(Arena *arena,Bindings *env) {
    const char *inputs[]={"(I)","(I (read Token) (read Token))",
        "(I (take Token) (take Token))","(I (all (p $x) $rows))",
        "(I (absent Stop))","(I (read (exec $key $in $out)))"};
    const char *outputs[]={"(O (+ Ready))","(O (+ Ready))","(O (+ Ready))",
        "(O (+ (Rows $rows)))","(O (+ Ready))","(O (+ (Self $key)))"};
    const char *expected[]={"(O (+ Ready))","(O (+ Ready))","(O (+ Ready))",
        "(O (+ (Rows ((p A) (p B)))))","(O (+ Ready))","(O (+ (Self Chosen)))"};
    for(size_t test=0u;test<sizeof(inputs)/sizeof(*inputs);test++) {
        Space space={0};space_init(&space);
        add(&space,arena,"Token");add(&space,arena,"Token");
        add(&space,arena,"(p A)");add(&space,arena,"(p B)");
        for(unsigned i=0u;i<128u;i++)add(&space,arena,"Unrelated");
        char text[256];snprintf(text,sizeof(text),"(exec Chosen %s %s)",inputs[test],outputs[test]);
        add(&space,arena,text);
        SpaceReadToken source=space_read_token(&space);
        CettaSpaceAgenda *copy=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(copy);
        CettaNativeCostLedger copied,borrowed;
        cetta_native_cost_init_compact(&copied);cetta_native_cost_init_compact(&borrowed);
        CettaNativeCostScope scope;
        cetta_native_cost_scope_enter(&scope,&copied,0u);
        CHECK(cetta_space_agenda_prepare_source(copy,source,space_length64(&space)-1u)==
            CETTA_SPACE_AGENDA_PROGRESS);
        until_phase(copy,"Build");cetta_native_cost_scope_leave(&scope);
        CHECK(copy->snapshot_ready && copy->current_ready && copy->row_count==1u);
        CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);CHECK(d);
        CettaSpaceAgendaChoice choice;
        cetta_native_cost_scope_enter(&scope,&borrowed,0u);
        CHECK(discover_choices(d,&choice,1u)==1u);
        cetta_native_cost_scope_leave(&scope);
        CHECK(d->ready[0]->borrowed_capture);
        CHECK(atom_alpha_eq(choice.outputs,parse(arena,expected[test])));
        CHECK(atom_alpha_eq_scoped(choice.outputs,copy->rows[0].outputs,
            d->ready[0]->match_epochs[0],d->ready[0]->match_epoch_count[0],
            copy->match_epochs[0],copy->match_epoch_count[0]));
        CHECK(atom_alpha_eq_scoped(choice.dependencies,copy->rows[0].dependencies,
            d->ready[0]->match_epochs[0],d->ready[0]->match_epoch_count[0],
            copy->match_epochs[0],copy->match_epoch_count[0]));
        CHECK(copied.total.counts[CETTA_COST_CANDIDATE_ROW]>=2u*space_length64(&space));
        CHECK(borrowed.total.counts[CETTA_COST_CANDIDATE_ROW]<32u);
        CHECK(space_read_token_is_current(source));
        CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,choice.ticket);
        CHECK(selected && !selected->agenda->borrowed_capture);
        cetta_space_agenda_selection_free(selected);cetta_space_agenda_discovery_free(d);
        cetta_space_agenda_free(copy);cetta_native_cost_free(&copied);cetta_native_cost_free(&borrowed);
        space_free(&space);
    }
    /* Non-atom-space ordering keeps the copied preparation contract. */
    Space space={0};space_init(&space);space.kind=SPACE_KIND_STACK;
    add(&space,arena,"(exec Chosen (I) (O (+ Ready)))");
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);CHECK(d);
    CettaSpaceAgendaChoice choice;CHECK(discover_choices(d,&choice,1u)==1u);
    CHECK(!d->ready[0]->borrowed_capture);
    cetta_space_agenda_discovery_free(d);space_free(&space);
    puts("PASS: borrowed discovery agrees with copied preparation on reads, consumes, aggregates, absence and self reads without copying unrelated rows");
}

static void borrowed_transactions(Arena *arena,Bindings *env) {
    const char *inputs[]={"(I)","(I (read Token) (read Token))",
        "(I (take Token) (take Token))","(I (all (p $x) $rows))",
        "(I (absent Stop))","(I (read (exec $key $in $out)))"};
    const char *outputs[]={"(O (+ Ready))","(O (+ Ready))","(O (+ Ready))",
        "(O (+ (Rows $rows)))","(O (+ Ready))","(O (+ (Self $key)))"};
    for(size_t test=0u;test<sizeof(inputs)/sizeof(*inputs);test++) {
        Space source={0},reference={0};space_init(&source);
        space_init_with_universe(&reference,source.universe);
        const char *facts[]={"Token","Token","(p A)","(p B)"};
        for(size_t i=0u;i<sizeof(facts)/sizeof(*facts);i++) {
            add(&source,arena,facts[i]);add(&reference,arena,facts[i]);
        }
        for(unsigned i=0u;i<128u;i++) {
            add(&source,arena,"Unrelated");add(&reference,arena,"Unrelated");
        }
        char text[256];snprintf(text,sizeof(text),"(exec Chosen %s %s)",inputs[test],outputs[test]);
        add(&source,arena,text);
        CHECK(space_admit_atom(&reference,arena,
            space_get_at64(&source,space_length64(&source)-1u)));
        CettaSpaceAgendaContract contract={.explicit_inputs=true};
        CettaSpaceAgenda *borrow=cetta_space_agenda_new(&source,arena,env,contract);
        CettaSpaceAgenda *copy=cetta_space_agenda_new(&reference,arena,env,contract);
        CHECK(borrow && copy);
        CettaNativeCostLedger copied,borrowed;
        cetta_native_cost_init_compact(&copied);cetta_native_cost_init_compact(&borrowed);
        CettaNativeCostScope scope;
        cetta_native_cost_scope_enter(&scope,&copied,0u);
        CHECK(cetta_space_agenda_prepare_source(copy,space_read_token(&reference),
            space_length64(&reference)-1u)==CETTA_SPACE_AGENDA_PROGRESS);
        until_phase(copy,"Build");cetta_native_cost_scope_leave(&scope);
        cetta_native_cost_scope_enter(&scope,&borrowed,0u);
        until_phase(borrow,"Build");cetta_native_cost_scope_leave(&scope);
        CHECK(borrow->borrowed_capture && !borrow->snapshot_ready && !borrow->current_ready);
        CHECK(!copy->borrowed_capture && copy->snapshot_ready && copy->current_ready);
        CHECK(borrow->row_count==1u && copy->row_count==1u);
        CHECK(atom_alpha_eq_scoped(borrow->rows[0].outputs,copy->rows[0].outputs,
            borrow->match_epochs[0],borrow->match_epoch_count[0],
            copy->match_epochs[0],copy->match_epoch_count[0]));
        if(!atom_alpha_eq_scoped(borrow->rows[0].dependencies,copy->rows[0].dependencies,
            borrow->match_epochs[0],borrow->match_epoch_count[0],
            copy->match_epochs[0],copy->match_epoch_count[0])) {
            fprintf(stderr,"borrowed Transaction dependency comparison %zu\n",test);
            atom_print(borrow->rows[0].dependencies,stderr);fputc('\n',stderr);
            atom_print(copy->rows[0].dependencies,stderr);fputc('\n',stderr);
        }
        CHECK(atom_alpha_eq_scoped(borrow->rows[0].dependencies,copy->rows[0].dependencies,
            borrow->match_epochs[0],borrow->match_epoch_count[0],
            copy->match_epochs[0],copy->match_epoch_count[0]));
        CHECK(copied.total.counts[CETTA_COST_CANDIDATE_ROW]>=2u*space_length64(&reference));
        CHECK(borrowed.total.counts[CETTA_COST_CANDIDATE_ROW]<32u);
        run(borrow,1u);run(copy,1u);
        CHECK(space_length64(&source)==space_length64(&reference));
        for(CettaIndex i=0u;i<space_length64(&source);i++)
            CHECK(atom_alpha_eq(space_get_at64(&source,i),space_get_at64(&reference,i)));
        cetta_space_agenda_free(copy);cetta_space_agenda_free(borrow);
        cetta_native_cost_free(&copied);cetta_native_cost_free(&borrowed);
        space_free(&source);space_free(&reference);
    }
    puts("PASS: ordinary borrowed Transaction reads preserve copied proposals, physical consumes and exact publication order without two whole-store read copies");
}

static void borrowed_capture_restart(Arena *arena,Bindings *env) {
    for(unsigned validating=0u;validating<2u;validating++) {
        Space space={0};space_init(&space);
        CHECK(space_admit_atom(&space,arena,wide_row(arena,atom_symbol(arena,"A"))));
        Atom *pattern=wide_row(arena,atom_var(arena,"$x"));
        Atom *rows=atom_var(arena,"$rows");
        Atom *all=atom_expr(arena,(Atom *[]){atom_symbol(arena,"all"),pattern,
            rows},3u);
        Atom *outputs=atom_expr2(arena,atom_symbol(arena,"O"),
            atom_expr2(arena,atom_symbol(arena,"+"),
                atom_expr2(arena,atom_symbol(arena,"Collected"),rows)));
        Atom *exec=atom_expr(arena,(Atom *[]){atom_symbol(arena,"exec"),
            atom_symbol(arena,"Collect"),atom_expr2(arena,atom_symbol(arena,"I"),all),
            outputs},4u);
        CHECK(space_admit_atom(&space,arena,exec));
        CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
        CettaSpaceAgendaMatch *match=NULL;
        for(unsigned i=0u;!match;i++) {
            CHECK(i<10000u);
            CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
            if(a->validating==!!validating)match=cetta_space_agenda_match_capture(a);
        }
        CHECK(a->borrowed_capture && !a->snapshot_ready && !a->current_ready);
        CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
        CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,0u);
        CHECK(cetta_space_agenda_match_run(match,7u));
        /* Seven matcher steps, as above: at most seven of them nodes. */
        const uint64_t paid=meter.total.counts[CETTA_COST_MATCH_NODE];
        CHECK(match->decision==CETTA_MATCH_CURSOR_PROGRESS);
        CHECK(paid>0u && paid<=7u);
        CHECK(space_admit_atom(&space,arena,wide_row(arena,atom_symbol(arena,"B"))));
        CHECK(!cetta_space_agenda_match_capture(a));
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_RETRY);
        CHECK(meter.total.counts[CETTA_COST_MATCH_NODE]==paid);
        CHECK(!a->matching && !a->snapshot_ready && !a->current_ready);
        CHECK(a->retries==1u);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&meter);
        run(a,1u);
        Atom *collected=space_get_at64(&space,space_length64(&space)-1u);
        CHECK(shaped(collected,symbol_intern_cstr(g_symbols,"Collected"),1u));
        if(collected->expr.elems[1]->expr.len!=2u) {
            fprintf(stderr,"borrowed capture restart validating=%u\n",validating);
            atom_print(collected,stderr);fputc('\n',stderr);
        }
        CHECK(collected->expr.elems[1]->expr.len==2u);
        CHECK(atom_eq(collected->expr.elems[1]->expr.elems[0],
            wide_row(arena,atom_symbol(arena,"A"))));
        CHECK(atom_eq(collected->expr.elems[1]->expr.elems[1],
            wide_row(arena,atom_symbol(arena,"B"))));
        cetta_space_agenda_free(a);space_free(&space);
    }
    Space space={0};space_init(&space);space.kind=SPACE_KIND_STACK;
    add(&space,arena,"Token");
    add(&space,arena,"(exec Take (I (take Token)) (O (+ Taken)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    until_phase(a,"Build");
    CHECK(!a->borrowed_capture && a->snapshot_ready && a->current_ready);
    run(a,1u);CHECK(space_contains_exact(&space,parse(arena,"Taken")));
    CHECK(!space_contains_exact(&space,parse(arena,"Token")));
    cetta_space_agenda_free(a);space_free(&space);
    space_init(&space);add(&space,arena,"Token");add(&space,arena,"Token");
    add(&space,arena,"(exec SetRead (I (take Token) (take Token)) (O (+ Overdraw)))");
    a=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(a);
    until_phase(a,"Capture");CHECK(!a->borrowed_capture);
    CHECK(until_pending(a)==0u);
    CHECK(!space_contains_exact(&space,parse(arena,"Overdraw")) && space_length64(&space)==3u);
    cetta_space_agenda_free(a);space_free(&space);
    puts("PASS: changed borrowed sources discard only stale matcher proposals, retain paid work and revalidate aggregates; stack reads retain copied storage");
}

static void discovery_selection(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    add(&space,arena,"Permit");
    add(&space,arena,"(exec 10 (I (take Permit)) (O (+ (Winner Alpha))))");
    add(&space,arena,"(exec 0 (I (take Permit)) (O (+ (Winner Beta))))");
    add(&space,arena,"(exec 20 (I (read Missing)) (O (+ Wrong)))");
    SpaceReadToken unchanged=space_read_token(&space);
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);
    CHECK(d && !d->cursor_ready && !d->exhausted && !d->count);
    CettaSpaceAgendaChoice views[2];
    CHECK(discover_choices(d,views,2u)==2u);
    CHECK(space_read_token_is_current(unchanged));
    CHECK(views[0].source_index==1u && views[1].source_index==2u);
    CHECK(views[0].ticket!=views[1].ticket && !d->preparing);
    for(size_t i=0u;i<d->count;i++) {
        CHECK(d->ready[i]->row_count==1u);
        CHECK(!d->ready[i]->snapshot_ready && !d->ready[i]->current_ready &&
              !d->ready[i]->replacement_ready && !d->ready[i]->receipt);
    }
    CHECK(!cetta_space_agenda_discovery_take(d,0u));
    CHECK(!cetta_space_agenda_discovery_take(NULL,views[1].ticket));
    CHECK(!cetta_space_agenda_discovery_take(d,99u));
    CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,views[1].ticket);
    CHECK(selected && !cetta_space_agenda_discovery_take(d,views[1].ticket));
    CettaSpaceAgendaChoice gone;
    CHECK(!cetta_space_agenda_discovery_choice(d,views[1].ticket,&gone));
    cetta_space_agenda_discovery_free(d);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(space_contains_exact(&space,parse(arena,"(Winner Beta)")));
    CHECK(!space_contains_exact(&space,parse(arena,"(Winner Alpha)")));
    CHECK(!space_contains_exact(&space,parse(arena,"Permit")));
    SpaceReadToken finished=space_read_token(&space);
    Atom *receipt=cetta_space_agenda_selection_receipt(selected);
    CHECK(receipt && cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
    CHECK(space_read_token_is_current(finished) &&
          cetta_space_agenda_selection_receipt(selected)==receipt);
    cetta_space_agenda_selection_free(selected);space_free(&space);
    puts("PASS: discovery prepares alternatives without publication; the selected owner publishes once");
}

static void discovery_match_bindings(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    add(&space,arena,"(Fact A)");add(&space,arena,"(Fact B)");add(&space,arena,"(Fact A)");
    add(&space,arena,"(exec Join (I (take (Fact $x))) (O (+ (Seen $x))))");
    SpaceReadToken before=space_read_token(&space);
    CettaSpaceAgendaDiscovery *ordinary=cetta_space_agenda_discovery_new(&space,arena,env);
    CettaSpaceAgendaChoice first;CHECK(ordinary && discover_choices(ordinary,&first,1u)==1u);
    CHECK(atom_eq(first.outputs,parse(arena,"(O (+ (Seen A)))")));
    cetta_space_agenda_discovery_free(ordinary);
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new_owned_kind(&space,env,
        CETTA_SPACE_CHOICE_TRANSACTION_MATCH);CHECK(d);
    size_t count=0u;
    CettaSpaceAgendaChoice views[2];
    for(unsigned i=0u;i<100000u && count<2u;i++) {
        CettaSpaceAgendaChoice view;
        CettaSpaceAgendaDiscoveryStep step=cetta_space_agenda_discovery_step(d,&view);
        CHECK(step==CETTA_SPACE_DISCOVERY_PROGRESS || step==CETTA_SPACE_DISCOVERY_CHOICE);
        if(step==CETTA_SPACE_DISCOVERY_CHOICE)views[count++]=view;
    }
    CHECK(count==2u && d->preparing && d->preparing->join_ready);
    CHECK(views[0].source_index==3u && views[1].source_index==3u);
    CHECK(atom_eq(views[0].outputs,parse(arena,"(O (+ (Seen A)))")));
    CHECK(atom_eq(views[1].outputs,parse(arena,"(O (+ (Seen B)))")));
    CHECK(space_read_token_is_current(before));
    CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,2u);CHECK(selected);
    CHECK(selected->agenda->owned_arena && selected->agenda->root==selected->agenda);
    cetta_space_agenda_discovery_free(d); /* Retires the still-running matcher. */
    add(&space,arena,"(Fact C)");
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(space_contains_exact(&space,parse(arena,"(Seen B)")));
    CHECK(!space_contains_exact(&space,parse(arena,"(Seen A)")));
    CHECK(!space_contains_exact(&space,parse(arena,"(Fact B)")));
    CHECK(space_length64(&space)==4u);
    before=space_read_token(&space);
    CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
    CHECK(space_read_token_is_current(before));
    cetta_space_agenda_selection_free(selected);space_free(&space);

    space_init(&space);
    for(unsigned i=0u;i<3u;i++)add(&space,arena,"Token");
    add(&space,arena,"(exec Pair (I (take Token) (take Token)) (O (+ Done)))");
    before=space_read_token(&space);
    d=cetta_space_agenda_discovery_new_owned_kind(&space,env,CETTA_SPACE_CHOICE_TRANSACTION_MATCH);
    CettaSpaceAgendaChoice choices[6];CHECK(d && discover_choices(d,choices,6u)==6u);
    CHECK(space_read_token_is_current(before));
    for(size_t i=0u;i<6u;i++) {
        CHECK(choices[i].ticket==i+1u && choices[i].source_index==3u);
        Atom *deps=choices[i].dependencies;
        CHECK(deps->expr.len==2u && !atom_eq(deps->expr.elems[0]->expr.elems[2],
            deps->expr.elems[1]->expr.elems[2]));
        for(size_t j=0u;j<i;j++)CHECK(!atom_eq(deps,choices[j].dependencies));
    }
    selected=cetta_space_agenda_discovery_take(d,6u);CHECK(selected);
    cetta_space_agenda_discovery_free(d);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(space_length64(&space)==2u && space_contains_exact(&space,parse(arena,"Done")));
    cetta_space_agenda_selection_free(selected);space_free(&space);

    for(unsigned variant=0u;variant<3u;variant++) {
        space_init(&space);add(&space,arena,"(Fact A)");add(&space,arena,"(Fact B)");
        add(&space,arena,"(Item 1)");
        add(&space,arena,"(exec Join (I (read (Fact $x)) (all (Item $y) $rows) (absent (Stop $x))) (O (+ (Seen $x $rows))))");
        d=cetta_space_agenda_discovery_new_owned_kind(&space,env,CETTA_SPACE_CHOICE_TRANSACTION_MATCH);
        CHECK(d && discover_choices(d,views,2u)==2u);
        selected=cetta_space_agenda_discovery_take(d,2u);CHECK(selected);
        cetta_space_agenda_discovery_free(d);
        add(&space,arena,variant==0u?"(Stop A)":variant==1u?"(Stop B)":"(Item 2)");
        before=space_read_token(&space);
        CettaSpaceAgendaStep result=perform_selection(selected);
        if(!variant) {
            CHECK(result==CETTA_SPACE_AGENDA_COMMITTED);
            CHECK(space_contains_exact(&space,parse(arena,"(Seen B ((Item 1)))")));
        } else {
            CHECK(result==CETTA_SPACE_AGENDA_PENDING && selected->stale);
            CHECK(selected->agenda->row_count==1u &&
                atom_eq(selected->agenda->rows[0].outputs,
                    parse(arena,"(O (+ (Seen B ((Item 1)))))")));
            CHECK(space_read_token_is_current(before));
            CHECK(!space_contains_exact(&space,parse(arena,"(Seen A ((Item 1)))")));
            CHECK(!space_contains_exact(&space,parse(arena,"(Seen B ((Item 1)))")));
        }
        cetta_space_agenda_selection_free(selected);space_free(&space);
    }
    space_init(&space);add(&space,arena,"(p (Pair $v $v))");
    add(&space,arena,"(exec Open (I (read (p $x))) (O (+ (Seen $x $x))))");
    d=cetta_space_agenda_discovery_new_owned_kind(&space,env,CETTA_SPACE_CHOICE_TRANSACTION_MATCH);
    CHECK(d && discover_choices(d,&first,1u)==1u);
    selected=cetta_space_agenda_discovery_take(d,1u);CHECK(selected);
    cetta_space_agenda_discovery_free(d);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(atom_alpha_eq(space_get_at64(&space,space_length64(&space)-1u),
        parse(arena,"(Seen (Pair $x $x) (Pair $x $x))")));
    cetta_space_agenda_selection_free(selected);space_free(&space);
    for(unsigned variant=0u;variant<2u;variant++) {
        space_init(&space);
        add(&space,arena,variant?"(exec Missing (I (take Absent)) (O (+ Wrong)))":
            "(exec Empty (I) (O (+ Ready)))");
        d=cetta_space_agenda_discovery_new_owned_kind(&space,env,CETTA_SPACE_CHOICE_TRANSACTION_MATCH);
        CHECK(d && discover_choices(d,&first,1u)==(variant?0u:1u));
        cetta_space_agenda_discovery_free(d);space_free(&space);
    }
    puts("PASS: every physical binding has an owned choice; exact selected revalidation preserves aliases and refuses changed absence/aggregates without retargeting");
}

static void discovery_currentness(Arena *arena,Bindings *env) {
    for(unsigned variant=0u;variant<3u;variant++) {
        Space space={0};space_init(&space);
        add(&space,arena,"Permit");
        add(&space,arena,"(exec 0 (I (take Permit) (absent Blocked)) (O (+ Winner)))");
        CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);
        CettaSpaceAgendaChoice view;
        CHECK(d && discover_choices(d,&view,1u)==1u);
        if(variant==0u)add(&space,arena,"Unrelated");
        else if(variant==1u)add(&space,arena,"Blocked");
        else CHECK(space_remove(&space,parse(arena,"Permit")));
        CettaSpaceAgendaChoice old;
        CHECK(cetta_space_agenda_discovery_choice(d,view.ticket,&old) && !old.current);
        CHECK(cetta_space_agenda_discovery_step(d,&old)==CETTA_SPACE_DISCOVERY_INVALIDATED);
        CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,view.ticket);
        CHECK(selected);cetta_space_agenda_discovery_free(d);
        SpaceReadToken before=space_read_token(&space);
        CettaSpaceAgendaStep step=perform_selection(selected);
        CHECK(step==(variant==0u ? CETTA_SPACE_AGENDA_COMMITTED : CETTA_SPACE_AGENDA_PENDING));
        CHECK(space_contains_exact(&space,parse(arena,"Winner"))==(variant==0u));
        if(variant)CHECK(space_read_token_is_current(before));
        if(variant==1u) {
            CHECK(space_contains_exact(&space,parse(arena,"Permit")));
            CHECK(!strcmp(cetta_space_agenda_selection_pending(selected),"ChangedPreparedChoice"));
        }
        cetta_space_agenda_selection_free(selected);space_free(&space);
    }
    Space space={0};space_init(&space);add(&space,arena,"(p A)");
    add(&space,arena,"(exec 0 (I (all (p $x) $rows)) (O (+ (Chosen $rows))))");
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);
    CettaSpaceAgendaChoice view;CHECK(d && discover_choices(d,&view,1u)==1u);
    CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,view.ticket);
    CHECK(selected);cetta_space_agenda_discovery_free(d);add(&space,arena,"(p B)");
    SpaceReadToken unchanged=space_read_token(&space);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(space_read_token_is_current(unchanged));
    CHECK(!strcmp(cetta_space_agenda_selection_pending(selected),"ChangedPreparedChoice"));
    CHECK(!space_contains_exact(&space,parse(arena,"(Chosen ((p A) (p B)))")));
    cetta_space_agenda_selection_free(selected);space_free(&space);
    puts("PASS: selected proposals revalidate unrelated changes but refuse changed absence, consumes and bindings");
}

static void discovery_duplicates_and_exhaustion(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);
    CHECK(d && !d->exhausted && !d->cursor_ready);
    CettaSpaceAgendaChoice empty;
    CHECK(cetta_space_agenda_discovery_step(d,&empty)==CETTA_SPACE_DISCOVERY_EXHAUSTED);
    CHECK(!empty.ticket && d->exhausted);
    add(&space,arena,"New");
    CHECK(cetta_space_agenda_discovery_step(d,&empty)==CETTA_SPACE_DISCOVERY_INVALIDATED);
    cetta_space_agenda_discovery_free(d);space_free(&space);

    memset(&space,0,sizeof(space));space_init(&space);add(&space,arena,"Permit");
    const char *exec="(exec 0 (I (take Permit)) (O (+ Winner)))";
    add(&space,arena,exec);add(&space,arena,exec);
    d=cetta_space_agenda_discovery_new(&space,arena,env);
    CettaSpaceAgendaChoice views[2];CHECK(d && discover_choices(d,views,2u)==2u);
    CHECK(atom_eq(views[0].source,views[1].source));
    CHECK(views[0].ticket!=views[1].ticket && views[0].source_index!=views[1].source_index);
    CettaSpaceAgendaSelection *left=cetta_space_agenda_discovery_take(d,views[0].ticket);
    CettaSpaceAgendaSelection *right=cetta_space_agenda_discovery_take(d,views[1].ticket);
    CHECK(left && right);cetta_space_agenda_discovery_free(d);
    CHECK(perform_selection(left)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(perform_selection(right)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(space_length64(&space)==2u);
    cetta_space_agenda_selection_free(left);cetta_space_agenda_selection_free(right);space_free(&space);

    memset(&space,0,sizeof(space));space_init(&space);
    add(&space,arena,"(exec 0 (I (unknown Input)) (O (+ Wrong)))");
    SpaceReadToken unchanged=space_read_token(&space);
    d=cetta_space_agenda_discovery_new(&space,arena,env);CHECK(d);
    CettaSpaceAgendaDiscoveryStep incomplete=CETTA_SPACE_DISCOVERY_PROGRESS;
    for(unsigned i=0u;i<1000u && incomplete==CETTA_SPACE_DISCOVERY_PROGRESS;i++)
        incomplete=cetta_space_agenda_discovery_step(d,&empty);
    CHECK(incomplete==CETTA_SPACE_DISCOVERY_INCOMPLETE && !d->exhausted);
    CHECK(!strcmp(cetta_space_agenda_discovery_pending(d),"UninterpretedExec"));
    CHECK(cetta_space_agenda_discovery_step(d,&empty)==CETTA_SPACE_DISCOVERY_INCOMPLETE);
    CHECK(space_read_token_is_current(unchanged));
    cetta_space_agenda_discovery_free(d);space_free(&space);
    puts("PASS: duplicate choices keep distinct sites; exhaustion is captured and consumers cannot spend a permit twice");
}

static void discovery_environment(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);add(&space,arena,"(fact A)");add(&space,arena,"(fact B)");
    Atom *exec=parse(arena,"(exec 0 (I (read (fact $outer))) (O (+ (kept $outer))))");
    Atom *outer=exec->expr.elems[2]->expr.elems[1]->expr.elems[1]->expr.elems[1];
    CHECK(space_admit_atom(&space,arena,exec));
    Bindings captured;CHECK(bindings_clone(&captured,env));
    CHECK(bindings_add_var(&captured,outer,atom_symbol(arena,"B")));
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,&captured);
    CHECK(d);bindings_free(&captured);
    CettaSpaceAgendaChoice view;CHECK(discover_choices(d,&view,1u)==1u);
    CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,view.ticket);
    CHECK(selected);cetta_space_agenda_discovery_free(d);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(space_contains_exact(&space,parse(arena,"(kept B)")));
    CHECK(!space_contains_exact(&space,parse(arena,"(kept A)")));
    cetta_space_agenda_selection_free(selected);space_free(&space);
    puts("PASS: a chosen interaction retains caller bindings after caller and discovery owners release them");
}

typedef struct {unsigned holds,releases;} ProposalLifetime;
static void proposal_retain(void *raw) {((ProposalLifetime *)raw)->holds++;}
static void proposal_release(void *raw) {
    ProposalLifetime *life=raw;CHECK(life->holds);life->holds--;life->releases++;
}

static void owned_proposal_lifetimes(Arena *source_arena,Bindings *env) {
    Space space={0};space_init(&space);
    add(&space,source_arena,"(fact A)");add(&space,source_arena,"(fact B)");
    Atom *exec=parse(source_arena,"(exec 0 (I (read (fact $outer))) (O (+ (kept $outer))))");
    Atom *outer=exec->expr.elems[2]->expr.elems[1]->expr.elems[1]->expr.elems[1];
    CHECK(space_admit_atom(&space,source_arena,exec));
    CHECK(space_admit_atom(&space,source_arena,exec));
    Arena caller;arena_init_detached(&caller);
    Bindings captured;CHECK(bindings_clone(&captured,env));
    CHECK(bindings_add_var(&captured,outer,atom_symbol(&caller,"B")));
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new_owned(&space,&captured);
    CHECK(d);bindings_free(&captured);arena_free(&caller);
    CettaSpaceAgendaChoice choices[2];CHECK(discover_choices(d,choices,2u)==2u);
    CHECK(choices[0].source_index!=choices[1].source_index);
    CHECK(d->ready[0]->owned_arena && d->ready[1]->owned_arena &&
          d->ready[0]->owned_arena!=d->ready[1]->owned_arena);
    ProposalLifetime selected_life={0},unchosen_life={0},capture_life={0};
    CHECK(arena_retain_owner(d->ready[0]->owned_arena,&selected_life,
                            proposal_retain,proposal_release));
    CHECK(arena_retain_owner(d->ready[1]->owned_arena,&unchosen_life,
                            proposal_retain,proposal_release));
    CHECK(arena_retain_owner(d->owner,&capture_life,proposal_retain,proposal_release));
    CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,choices[0].ticket);
    CHECK(selected && !cetta_space_agenda_discovery_take(d,choices[0].ticket));
    cetta_space_agenda_discovery_free(d);
    CHECK(!unchosen_life.holds && unchosen_life.releases==1u);
    CHECK(!capture_life.holds && capture_life.releases==1u);
    CHECK(selected_life.holds==1u && !selected_life.releases);
    CHECK(perform_selection(selected)==CETTA_SPACE_AGENDA_COMMITTED);
    CHECK(space_contains_exact(&space,parse(source_arena,"(kept B)")));
    CHECK(!space_contains_exact(&space,parse(source_arena,"(kept A)")));
    CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
    cetta_space_agenda_selection_free(selected);
    CHECK(!selected_life.holds && selected_life.releases==1u);
    space_free(&space);
    puts("PASS: private proposals outlive caller captures and the discovery; untaken proposals and the catalogue capture retire independently");
}

typedef struct {unsigned events,commits,permits;uint64_t charges;} SelectionNotes;
static bool selection_observe(void *raw,const CettaSpaceAgendaEvent *event) {
    SelectionNotes *notes=raw;notes->events++;
    if(event->kind==CETTA_AGENDA_EVENT_COMMITTED)notes->commits++;
    return true;
}
static bool selection_permit(void *raw,const CettaSpaceAgendaEvent *event) {
    CHECK(event->kind==CETTA_AGENDA_EVENT_PRECOMMIT);
    ((SelectionNotes *)raw)->permits++;return true;
}
static bool selection_charge(void *raw,const CettaSpaceAgenda *agenda,
        CettaSpaceAgendaChargeSite site,unsigned kind,uint64_t units,bool after) {
    (void)agenda;(void)site;(void)kind;
    if(after)((SelectionNotes *)raw)->charges+=units;
    return true;
}

static void selection_provider_scope(Arena *arena,Bindings *env) {
    for(unsigned recorded=0u;recorded<3u;recorded++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(exec 0 (I) (O (+ First)))");
        add(&space,arena,"(exec 1 (I) (O (+ Second)))");
        CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new(&space,arena,env);
        CettaSpaceAgendaChoice views[2];CHECK(d && discover_choices(d,views,2u)==2u);
        CettaSpaceAgendaSelection *selected=cetta_space_agenda_discovery_take(d,views[1].ticket);
        CettaSpaceAgendaSelection *other=cetta_space_agenda_discovery_take(d,views[0].ticket);
        CHECK(selected && other);cetta_space_agenda_discovery_free(d);
        CettaSpaceAgenda *provider=cetta_space_agenda_selection_provider(selected);
        SelectionNotes notes={0};CettaSpaceAgendaObservationScope observation={0};
        CettaSpaceAgendaSelectionScope scope={0},reentrant={0};
        if(recorded==1u)CHECK(cetta_space_agenda_observation_enter(&observation,provider,
            selection_observe,selection_permit,selection_charge,&notes));
        CHECK(cetta_space_agenda_selection_enter(&scope,selected));
        CHECK(!cetta_space_agenda_selection_enter(&reentrant,selected));
        CHECK(!cetta_space_agenda_selection_enter(&scope,other));
        CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_PENDING);
        if(recorded==2u)CHECK(cetta_space_agenda_observation_enter(&observation,provider,
            selection_observe,selection_permit,selection_charge,&notes));
        SpaceReadToken before=space_read_token(&space);
        for(unsigned i=0u;i<5u;i++) {
            CettaSpaceAgendaStep step=cetta_space_agenda_step(provider);
            CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
        }
        CHECK(!selected->completed && space_read_token_is_current(before));
        cetta_space_agenda_selection_leave(&scope);
        CHECK(!selected->leased && cetta_space_agenda_selection_enter(&scope,selected));
        unsigned commits=0u;bool closed=false;
        for(unsigned i=0u;i<10000u;i++) {
            CettaSpaceAgendaStep step=cetta_space_agenda_step(provider);
            if(step==CETTA_SPACE_AGENDA_COMMITTED) {
                commits++;
                if(commits==1u) {
                    CHECK(space_contains_exact(&space,parse(arena,"Second")));
                    CHECK(!space_contains_exact(&space,parse(arena,"First")));
                }
            }
            if(step==CETTA_SPACE_AGENDA_CLOSED) {closed=true;break;}
            CHECK(step!=CETTA_SPACE_AGENDA_PENDING);
        }
        CHECK(closed && commits==2u && selected->completed);
        CHECK(space_contains_exact(&space,parse(arena,"First")));
        CHECK(cetta_space_agenda_selection_receipt(selected) &&
              cetta_space_agenda_selection_receipt(selected)!=cetta_space_agenda_receipt(provider));
        if(recorded==2u)cetta_space_agenda_observation_leave(&observation);
        cetta_space_agenda_selection_leave(&scope);
        if(recorded==1u)cetta_space_agenda_observation_leave(&observation);
        if(recorded) {
            CHECK(notes.commits==2u && notes.permits==2u && notes.events>0u && notes.charges>0u);
            CHECK(cetta_space_agenda_observation_complete(&observation));
        } else CHECK(!notes.events && !notes.charges);
        CHECK(agenda_operations==&agenda_plain_operations && !agenda_selection);
        CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
        cetta_space_agenda_selection_free(selected);cetta_space_agenda_selection_free(other);
        space_free(&space);
    }
    puts("PASS: scoped selection drives the common provider, resumes, composes with recording and continues normally");
}

static void nested_selection_scopes(Arena *arena,Bindings *env) {
    Space left={0},right={0};space_init(&left);space_init(&right);
    add(&left,arena,"(p A)");
    add(&left,arena,"(exec 0 (I (all (p $x) $rows)) (O (+ (Left $rows))))");
    add(&right,arena,"(exec 0 (I) (O (+ Right)))");
    CettaSpaceAgendaDiscovery *ld=cetta_space_agenda_discovery_new(&left,arena,env);
    CettaSpaceAgendaDiscovery *rd=cetta_space_agenda_discovery_new(&right,arena,env);
    CettaSpaceAgendaChoice lv,rv;
    CHECK(ld && rd && discover_choices(ld,&lv,1u)==1u && discover_choices(rd,&rv,1u)==1u);
    CettaSpaceAgendaSelection *ls=cetta_space_agenda_discovery_take(ld,lv.ticket);
    CettaSpaceAgendaSelection *rs=cetta_space_agenda_discovery_take(rd,rv.ticket);
    CHECK(ls && rs);cetta_space_agenda_discovery_free(ld);cetta_space_agenda_discovery_free(rd);
    CettaSpaceAgendaSelectionScope outer={0},inner={0};
    CHECK(cetta_space_agenda_selection_enter(&outer,ls));
    CHECK(cetta_space_agenda_selection_enter(&inner,rs));
    CettaSpaceAgendaStep done=CETTA_SPACE_AGENDA_PROGRESS;
    for(unsigned i=0u;i<10000u && done!=CETTA_SPACE_AGENDA_COMMITTED;i++) {
        done=cetta_space_agenda_step(cetta_space_agenda_selection_provider(rs));
        CHECK(done==CETTA_SPACE_AGENDA_PROGRESS || done==CETTA_SPACE_AGENDA_RETRY ||
              done==CETTA_SPACE_AGENDA_COMMITTED);
    }
    CHECK(done==CETTA_SPACE_AGENDA_COMMITTED && rs->completed && !ls->completed);
    add(&left,arena,"(p B)");SpaceReadToken unchanged=space_read_token(&left);
    CettaSpaceAgendaStep pending=CETTA_SPACE_AGENDA_PROGRESS;
    for(unsigned i=0u;i<10000u && pending!=CETTA_SPACE_AGENDA_PENDING;i++) {
        pending=cetta_space_agenda_step(cetta_space_agenda_selection_provider(ls));
        CHECK(pending==CETTA_SPACE_AGENDA_PROGRESS || pending==CETTA_SPACE_AGENDA_RETRY ||
              pending==CETTA_SPACE_AGENDA_PENDING);
    }
    CHECK(pending==CETTA_SPACE_AGENDA_PENDING && ls->stale);
    CHECK(!strcmp(cetta_space_agenda_selection_pending(ls),"ChangedPreparedChoice"));
    CHECK(space_read_token_is_current(unchanged));
    cetta_space_agenda_selection_leave(&inner);cetta_space_agenda_selection_leave(&outer);
    CHECK(agenda_operations==&agenda_plain_operations);
    cetta_space_agenda_selection_free(ls);cetta_space_agenda_selection_free(rs);
    space_free(&left);space_free(&right);
    puts("PASS: completing an inner selection preserves the outer selection's binding constraint");
}

static void selected_world_commit(CettaSpaceAgendaSelection *selected) {
    CettaSpaceAgenda *provider=cetta_space_agenda_selection_provider(selected);
    CettaSpaceAgendaSelectionScope scope={0};
    CHECK(cetta_space_agenda_selection_enter(&scope,selected));
    for(unsigned i=0u;!selected->completed;i++) {
        CHECK(i<10000u);
        CettaSpaceAgendaStep step=cetta_space_agenda_step(provider);
        CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
        if(i%3u==0u) {
            cetta_space_agenda_selection_leave(&scope);
            CHECK(cetta_space_agenda_selection_enter(&scope,selected));
        }
    }
    cetta_space_agenda_selection_leave(&scope);
    CHECK(provider->contract.snapshot_worlds && provider->space==&provider->world);
    CHECK(cetta_space_agenda_selection_step(selected)==CETTA_SPACE_AGENDA_CLOSED);
    CHECK(cetta_space_agenda_selection_receipt(selected) &&
        atom_is_symbol(cetta_space_agenda_selection_receipt(selected)->expr.elems[0],"WorldStep"));
}

static size_t finish_selected_world(CettaSpaceAgenda *provider,Atom **reports,size_t capacity) {
    CettaSpaceAgenda *work[16]={provider};size_t count=1u,reported=0u;
    for(unsigned i=0u;count;i++) {
        CHECK(i<100000u);
        CettaSpaceAgenda *current=work[count-1u];
        CettaSpaceAgendaStep step=cetta_space_agenda_step(current);
        if(step==CETTA_SPACE_AGENDA_FORK) {
            CettaSpaceAgenda *child=cetta_space_agenda_child(current);
            CHECK(child && cetta_space_agenda_accept_child(current,child));
            CHECK(count<sizeof(work)/sizeof(*work));work[count++]=child;
        } else if(step==CETTA_SPACE_AGENDA_WORLD) {
            CHECK(reported<capacity);reports[reported++]=cetta_space_agenda_receipt(current);
        } else if(step==CETTA_SPACE_AGENDA_CLOSED) count--;
        else CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
    }
    return reported;
}

static void discovery_selected_worlds(Arena *arena,Bindings *env) {
    Space parent={0};space_init(&parent);
    add(&parent,arena,"(Fact A)");add(&parent,arena,"(Fact B)");add(&parent,arena,"(Fact A)");
    add(&parent,arena,"(exec Pick (I (take (Fact $x))) (O (+ (Seen $x))))");
    SpaceReadToken original=space_read_token(&parent);
    CettaSpaceAgendaDiscovery *d=cetta_space_agenda_discovery_new_owned_kind(&parent,env,
        CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH);
    CettaSpaceAgendaChoice views[3];CHECK(d && discover_choices(d,views,3u)==3u);
    for(size_t i=0u;i<3u;i++) {
        CHECK(views[i].kind==CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH);
        CHECK(views[i].dependencies->expr.elems[0]->expr.elems[2]->ground.ival==(int64_t)i);
        CHECK(!d->ready[i]->world_ready && !d->ready[i]->contract.snapshot_worlds);
    }
    CettaSpaceAgendaSelection *left=cetta_space_agenda_discovery_take(d,2u);
    CettaSpaceAgendaSelection *right=cetta_space_agenda_discovery_take(d,3u);
    CHECK(left && right);cetta_space_agenda_discovery_free(d);
    selected_world_commit(left);
    CettaSpaceAgenda *lp=cetta_space_agenda_selection_provider(left);
    CHECK(space_read_token_is_current(original) && space_length64(&parent)==4u);
    CHECK(space_contains_exact(lp->space,parse(arena,"(Seen B)")));
    CHECK(!space_contains_exact(lp->space,parse(arena,"(Fact B)")));
    CHECK(space_length64(lp->space)==3u && !right->started);
    selected_world_commit(right);
    CettaSpaceAgenda *rp=cetta_space_agenda_selection_provider(right);
    CHECK(space_read_token_is_current(original) && lp->space!=rp->space);
    CHECK(lp->space->instance_id!=rp->space->instance_id);
    CHECK(lp->space->instance_id!=parent.instance_id && rp->space->instance_id!=parent.instance_id);
    CHECK(space_contains_exact(rp->space,parse(arena,"(Seen A)")));
    CHECK(space_contains_exact(rp->space,parse(arena,"(Fact B)")));
    Atom *reports[2];CHECK(finish_selected_world(lp,reports,2u)==1u);
    CHECK(finish_selected_world(rp,reports,2u)==1u);
    CHECK(space_read_token_is_current(original));
    cetta_space_agenda_selection_free(left);cetta_space_agenda_selection_free(right);space_free(&parent);

    /* Even an unrelated parent append invalidates this exact snapshot
     * profile, before selection and after the first private publication. */
    for(unsigned after=0u;after<2u;after++) {
        space_init(&parent);add(&parent,arena,"Permit");
        add(&parent,arena,"(exec Start (I (take Permit)) (O (+ Ready)))");
        d=cetta_space_agenda_discovery_new_owned_kind(&parent,env,
            CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH);
        CHECK(d && discover_choices(d,views,1u)==1u);
        left=cetta_space_agenda_discovery_take(d,1u);CHECK(left);
        cetta_space_agenda_discovery_free(d);
        if(after)selected_world_commit(left);
        add(&parent,arena,"Unrelated");original=space_read_token(&parent);
        lp=cetta_space_agenda_selection_provider(left);
        CHECK(after?cetta_space_agenda_step(lp)==CETTA_SPACE_AGENDA_PENDING:
            cetta_space_agenda_selection_step(left)==CETTA_SPACE_AGENDA_PENDING);
        CHECK(space_read_token_is_current(original));
        CHECK(space_contains_exact(&parent,parse(arena,"Permit")));
        CHECK(!space_contains_exact(&parent,parse(arena,"Ready")));
        CHECK(after?!cetta_space_agenda_current(lp):left->stale);
        cetta_space_agenda_selection_free(left);space_free(&parent);
    }

    /* The chosen first publication rejoins the existing world evaluator.
     * Its created executable still forks on all later physical bindings. */
    space_init(&parent);add(&parent,arena,"Token");
    add(&parent,arena,"(Fact A)");add(&parent,arena,"(Fact B)");
    add(&parent,arena,"(exec Start (I (take Token)) (O (+ (exec Next (I (take (Fact $x))) (O (+ (Done $x)))))))");
    original=space_read_token(&parent);
    d=cetta_space_agenda_discovery_new_owned_kind(&parent,env,
        CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH);
    CHECK(d && discover_choices(d,views,1u)==1u);
    left=cetta_space_agenda_discovery_take(d,1u);CHECK(left);
    cetta_space_agenda_discovery_free(d);selected_world_commit(left);
    lp=cetta_space_agenda_selection_provider(left);
    CHECK(finish_selected_world(lp,reports,2u)==2u);
    CHECK(space_read_token_is_current(original));
    bool seen_a=false,seen_b=false;
    for(CettaSpaceAgenda *node=lp;node;node=node->owned_next) {
        if(node->phase!=AGENDA_DONE || node==lp)continue;
        seen_a|=space_contains_exact(node->space,parse(arena,"(Done A)"));
        seen_b|=space_contains_exact(node->space,parse(arena,"(Done B)"));
    }
    CHECK(seen_a && seen_b && !space_contains_exact(&parent,parse(arena,"(Done A)")));
    cetta_space_agenda_selection_free(left);space_free(&parent);
    CHECK(agenda_operations==&agenda_plain_operations && !agenda_selection);
    puts("PASS: selected physical tuples publish into separate owned worlds and resume ordinary world execution");
}

int main(void) {
    SymbolTable symbols;VarInternTable vars;Arena arena;Bindings env;
    symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    var_intern_init(&vars);g_var_intern=&vars;g_hashcons=NULL;arena_init(&arena);bindings_init(&env);
    scoped_alpha(&arena);captured_environment(&arena,&env);alpha_set_members(&arena,&env);
    mm2(&arena,&env);self_and_new_exec(&arena,&env);self_reintroduced(&arena,&env);transaction(&arena,&env);
    controlled_mm2_activation(&arena,&env);
    disabled_then_enabled(&arena,&env);shared_reads_and_takes(&arena,&env);
    phantoms(&arena,&env);replaced_occurrence(&arena,&env);
    world_fork_ownership(&arena,&env);
    prepared_match_ownership(&arena,&env);
    partial_match_authority(&arena,&env);
    retained_match_quanta(&arena,&env);
    retained_schedule(&arena,&env);
    raw_activation_discovery(&arena,&env);
    continuing_request_reply(&arena,&env);
    borrowed_discovery(&arena,&env);
    borrowed_transactions(&arena,&env);
    borrowed_capture_restart(&arena,&env);
    discovery_selection(&arena,&env);
    discovery_match_bindings(&arena,&env);
    discovery_currentness(&arena,&env);
    discovery_duplicates_and_exhaustion(&arena,&env);
    discovery_environment(&arena,&env);
    owned_proposal_lifetimes(&arena,&env);
    selection_provider_scope(&arena,&env);
    nested_selection_scopes(&arena,&env);
    discovery_selected_worlds(&arena,&env);
    bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
    puts("PASS: native space agenda joins, restores self reads, creates execs and consumes physical bag occurrences");
    printf("%zu checks passed\n",checks);return 0;
}
