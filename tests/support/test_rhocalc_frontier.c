#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "eval.h"
#include "symbol.h"
#include "../../src/rhocalc_core.c"

static unsigned checks,failures;
#define CHECK(test) do { checks++; if(!(test)) { \
    fprintf(stderr,"FAIL frontier line %u: %s\n",(unsigned)__LINE__,#test); failures++; return false; } } while(0)

static bool same_frontier(Arena *source,Atom *input,uint64_t quantum) {
    Arena expected,actual;
    arena_init_detached(&expected);arena_init_detached(&actual);
    Atom *oracle=rhocalc_successor_frontier_expr(&expected,input);
    CHECK(oracle && oracle->kind==ATOM_EXPR && oracle->expr.len==2u);
    Atom *reference=oracle->expr.elems[1];
    RhocalcFrontier *owner=rhocalc_frontier_open_held(&actual,input);
    CHECK(owner);
    Atom *tuple=NULL;
    uint64_t used=99u;
    CHECK(rhocalc_frontier_poll(owner,0u,&used)==RHOCALC_RUN_PENDING && used==0u);
    CHECK(rhocalc_frontier_account(owner).driver_steps==0u && !rhocalc_frontier_result(owner,&tuple));
    uint64_t total=0u;
    RhocalcRunStatus status;
    do {
        CHECK(!rhocalc_frontier_result(owner,&tuple));
        status=rhocalc_frontier_poll(owner,quantum,&used);
        total+=used;
        CHECK(used<=quantum && rhocalc_frontier_account(owner).driver_steps==total);
        if(status==RHOCALC_RUN_FAILED)fprintf(stderr,"frontier: %s\n",rhocalc_frontier_error(owner));
        CHECK(status!=RHOCALC_RUN_FAILED && total<UINT64_C(10000000));
    } while(status==RHOCALC_RUN_PENDING);
    CHECK(rhocalc_frontier_result(owner,&tuple));
    CHECK(tuple && tuple->kind==ATOM_EXPR && tuple->expr.len==reference->expr.len);
    rhocalc_frontier_close(owner);
    for(CettaExprIndex i=0u;i<tuple->expr.len;i++) {
        char *left=rho_key_proc(tuple->expr.elems[i]);
        char *right=rho_key_proc(reference->expr.elems[i]);
        CHECK(left && right && strcmp(left,right)==0);
        free(left);free(right);
    }
    CHECK(rhocalc_process_well_formed(input));
    arena_free(&actual);arena_free(&expected);
    (void)source;
    return true;
}

static bool cancellation(Arena *source,Atom *input) {
    for(unsigned cut=0u;cut<96u;cut++) {
        Arena result;arena_init_detached(&result);
        RhocalcFrontier *owner=rhocalc_frontier_open_held(&result,input);
        CHECK(owner);
        CHECK(rhocalc_frontier_poll(owner,cut,NULL)!=RHOCALC_RUN_FAILED);
        uint64_t used=77u;
        CHECK(rhocalc_frontier_release_poll(&owner,0u,false,&used)==CETTA_OWNED_RELEASE_PENDING && used==0u && owner);
        CettaOwnedReleaseStep step;
        do {
            step=rhocalc_frontier_release_poll(&owner,1u,false,&used);
            CHECK(used<=1u && owner);
        } while(step==CETTA_OWNED_RELEASE_PENDING);
        CHECK(step==CETTA_OWNED_RELEASE_ATOMIC);
        rhocalc_frontier_close(owner);
        arena_free(&result);
    }
    (void)source;
    return true;
}

static bool cancellation_in_each_phase(Atom *input) {
    for(unsigned phase=RHO_FRONTIER_SOURCE;phase<=RHO_FRONTIER_EXPORT;phase++) {
        Arena result;arena_init_detached(&result);
        RhocalcFrontier *owner=rhocalc_frontier_open_held(&result,input);
        CHECK(owner);
        uint64_t steps=0u;
        while((unsigned)owner->phase!=phase) {
            CHECK(rhocalc_frontier_poll(owner,1u,NULL)==RHOCALC_RUN_PENDING);
            CHECK(++steps<UINT64_C(1000000));
        }
        uint64_t used=77u;
        CHECK(rhocalc_frontier_release_poll(&owner,0u,false,&used)==CETTA_OWNED_RELEASE_PENDING && used==0u);
        CettaOwnedReleaseStep status;
        do {
            status=rhocalc_frontier_release_poll(&owner,1u,false,&used);
            CHECK(used<=1u && owner);
        } while(status==CETTA_OWNED_RELEASE_PENDING);
        CHECK(status==CETTA_OWNED_RELEASE_ATOMIC);
        Atom *tuple=NULL;
        CHECK(!rhocalc_frontier_result(owner,&tuple));
        CHECK(rhocalc_frontier_poll(owner,1u,NULL)==RHOCALC_RUN_FAILED);
        rhocalc_frontier_close(owner);arena_free(&result);
    }
    return true;
}

static bool malformed(Arena *source) {
    Arena result;arena_init_detached(&result);
    Atom *bad=rho_unary(source,"rho:send",rho_nil(source));
    RhocalcFrontier *owner=rhocalc_frontier_open_held(&result,bad);
    CHECK(owner);
    while(rhocalc_frontier_poll(owner,1u,NULL)==RHOCALC_RUN_PENDING) {}
    Atom *tuple=NULL;
    CHECK(rhocalc_frontier_error(owner) && rhocalc_frontier_invalid_input(owner) &&
        !rhocalc_frontier_result(owner,&tuple));
    rhocalc_frontier_close(owner);arena_free(&result);
    return true;
}

static bool engine_refusal_is_not_bad_input(Arena *source,Atom *input) {
    Arena result;arena_init_detached(&result);
    RhocalcFrontier *owner=rhocalc_frontier_open_held(&result,input);
    CHECK(owner);
    /* Exercise the representational capacity boundary without allocating
     * billions of preceding candidate rows. No grammar verdict is supplied. */
    owner->phase=RHO_FRONTIER_STORE;owner->count=UINT32_MAX;
    CHECK(rhocalc_frontier_poll(owner,1u,NULL)==RHOCALC_RUN_FAILED);
    CHECK(!rhocalc_frontier_invalid_input(owner));
    rhocalc_frontier_close(owner);arena_free(&result);
    (void)source;
    return true;
}

int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    g_hashcons=NULL;g_var_intern=NULL;
    Arena source;arena_init_detached(&source);
    Atom *nil=rho_nil(&source),*channel=rho_unary(&source,"rho:quote",nil);
    Atom *variable=atom_var(&source,"message");
    Atom *send=rho_binary(&source,"rho:send",channel,nil);
    Atom *receive=rho_ternary(&source,"rho:recv",channel,variable,rho_unary(&source,"rho:drop",variable));
    Atom *choice_parts[]={atom_symbol(&source,"rho:par"),send,send,receive};
    Atom *duplicate=atom_expr(&source,choice_parts,4u);
    Atom *other_channel=rho_unary(&source,"rho:quote",send);
    Atom *different_parts[]={atom_symbol(&source,"rho:par"),send,receive,
        rho_binary(&source,"rho:send",other_channel,nil),
        rho_ternary(&source,"rho:recv",other_channel,variable,send)};
    Atom *different=atom_expr(&source,different_parts,5u);
    Atom *drop=rho_unary(&source,"rho:drop",variable);
    Atom *duplication=rho_ternary(&source,"rho:recv",channel,variable,
        rho_binary(&source,"rho:par",rho_binary(&source,"rho:send",channel,drop),drop));
    Atom *replication=rho_binary(&source,"rho:par",
        rho_binary(&source,"rho:send",channel,rho_binary(&source,"rho:par",duplication,send)),duplication);
    Atom *inputs[]={nil,send,duplicate,different,replication};
    bool okay=true;
    for(size_t i=0u;okay && i<sizeof(inputs)/sizeof(*inputs);i++)
        okay=same_frontier(&source,inputs[i],1u) && same_frontier(&source,inputs[i],37u);
    okay=okay && cancellation(&source,different) && malformed(&source) &&
        engine_refusal_is_not_bad_input(&source,nil) && cancellation_in_each_phase(different);
    arena_free(&source);symbol_table_free(&symbols);g_symbols=NULL;
    printf("Rho frontier: %u checks, %u failures\n",checks,failures);
    return okay && !failures ? 0 : 1;
}
