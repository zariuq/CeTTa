/* Inspect the retained descendant state in the actual implementation. */
#include <stdbool.h>
#include <stdlib.h>
#include "space.h"
static _Thread_local bool agenda_test_refuse_calloc;
static _Thread_local unsigned agenda_test_refusals;
static _Thread_local bool agenda_test_refuse_admission;
static _Thread_local unsigned agenda_test_admission_refusals;
static _Thread_local bool agenda_test_refuse_removal;
static _Thread_local unsigned agenda_test_removal_refusals;
static _Thread_local bool agenda_test_refuse_occurrence;
struct Space;
struct Arena;
struct Atom;
static bool agenda_test_space_admit_atom(struct Space *,struct Arena *,struct Atom *);
static __attribute__((unused)) bool agenda_test_space_remove(struct Space *,struct Atom *);
static CettaMatchCompletion agenda_test_space_remove_checked(struct Space *,struct Atom *);
static CettaMatchCompletion agenda_test_space_remove_occurrence_checked(struct Space *,SpaceReadToken,CettaIndex);
static SpaceRemovalSearchStep agenda_test_removal_search_step(SpaceRemovalSearch *,struct Atom *,
    SpaceAtomComparison,SpaceAtomComparison,const uint8_t *,CettaCount);
static void *agenda_test_calloc(size_t count,size_t width) {
    if (agenda_test_refuse_calloc) {
        agenda_test_refuse_calloc=false;agenda_test_refusals++;
        return NULL;
    }
    return calloc(count,width);
}
#define calloc agenda_test_calloc
#define space_admit_atom agenda_test_space_admit_atom
#define space_remove agenda_test_space_remove
#define space_remove_checked agenda_test_space_remove_checked
#define space_remove_occurrence_checked agenda_test_space_remove_occurrence_checked
#define space_removal_search_step agenda_test_removal_search_step
#ifdef CETTA_AGENDA_TEST_MUTATION_DOUBLE_INDEX_VISIT
#include "native_cost.h"
static uint64_t agenda_mutant_cost_charge(CettaNativeCostKind kind,uint64_t units) {
    return cetta_native_cost_charge(kind,kind==CETTA_COST_INDEX_VISIT ? units*2u : units);
}
#define cetta_native_cost_charge agenda_mutant_cost_charge
#endif
#include "../src/native_space_agenda.c"
#ifdef CETTA_AGENDA_TEST_MUTATION_DOUBLE_INDEX_VISIT
#undef cetta_native_cost_charge
#endif
#undef space_admit_atom
#undef space_remove
#undef space_remove_checked
#undef space_remove_occurrence_checked
#undef space_removal_search_step
#undef calloc
extern bool space_admit_atom(struct Space *,struct Arena *,struct Atom *);
static bool agenda_test_space_admit_atom(struct Space *space,struct Arena *arena,struct Atom *atom) {
    if (agenda_test_refuse_admission) {
        agenda_test_refuse_admission=false;agenda_test_admission_refusals++;
        return false;
    }
    return space_admit_atom(space,arena,atom);
}
static CettaMatchCompletion agenda_test_space_remove_checked(struct Space *space,struct Atom *atom) {
    if (agenda_test_refuse_removal) {
        agenda_test_refuse_removal=false;agenda_test_removal_refusals++;
        return CETTA_MATCH_INCOMPLETE;
    }
    return space_remove_checked(space,atom);
}
static CettaMatchCompletion agenda_test_space_remove_occurrence_checked(struct Space *space,
        SpaceReadToken read,CettaIndex index) {
    if (agenda_test_refuse_removal || agenda_test_refuse_occurrence) {
        agenda_test_refuse_removal=agenda_test_refuse_occurrence=false;
        agenda_test_removal_refusals++;
        return CETTA_MATCH_INCOMPLETE;
    }
    return space_remove_occurrence_checked(space,read,index);
}
static SpaceRemovalSearchStep agenda_test_removal_search_step(SpaceRemovalSearch *search,
        struct Atom *body,SpaceAtomComparison equal,SpaceAtomComparison alpha,
        const uint8_t *protected_occurrences,CettaCount length) {
    if (agenda_test_refuse_removal) {
        agenda_test_refuse_removal=false;agenda_test_removal_refusals++;
        return SPACE_REMOVAL_INCOMPLETE;
    }
    return space_removal_search_step(search,body,equal,alpha,protected_occurrences,length);
}
static bool agenda_test_space_remove(struct Space *space,struct Atom *atom) {
    return agenda_test_space_remove_checked(space,atom)==CETTA_MATCH_MATCHED;
}
#include "parser.h"
#include "symbol.h"
#include "binding/frame_identity.h"
#include "native_cost.h"
#include "storage_census.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t checks;
#ifdef CETTA_AGENDA_TEST_MUTATION_DOUBLE_INDEX_VISIT
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);exit(1);}checks++;}while(0)
#else
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);abort();}checks++;}while(0)
#endif
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

/* A raw MM2 removal is staged until the sink batch is finalized; an
 * explicit bag transaction applies its written sinks in order. */
static int sink_order_controls(Arena *arena,Bindings *env) {
    bool observed[2][2]={{false}};
    for(unsigned raw=0u;raw<2u;raw++) for(unsigned reverse=0u;reverse<2u;reverse++) {
        Space space={0};space_init(&space);
        const char *program=raw
            ? (reverse ? "(exec 0 (,) (O (- A) (+ A)))" : "(exec 0 (,) (O (+ A) (- A)))")
            : (reverse ? "(exec 0 (I) (O (- A) (+ A)))" : "(exec 0 (I) (O (+ A) (- A)))");
        add(&space,arena,program);
        CettaSpaceAgendaContract contract=raw
            ? (CettaSpaceAgendaContract){.set_support=true,.raw_mm2_order=true}
            : (CettaSpaceAgendaContract){.explicit_inputs=true};
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,contract);
        CHECK(agenda);
        if(raw) {
            for(unsigned i=0u;i<10000u && agenda->phase!=AGENDA_SINK;i++)
                CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(agenda->phase==AGENDA_SINK && !agenda->sink_removal_phase);
            SpaceReadToken before=space_read_token(&space);
            for(unsigned i=0u;i<10000u && !agenda->sink_removal_phase;i++)
                CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(agenda->phase==AGENDA_SINK && agenda->sink_removal_phase &&
                space_read_token_is_current(before) && !space_contains_exact(&space,parse(arena,"A")));
        }
        run(agenda,1u);
        observed[raw][reverse]=space_contains_exact(&space,parse(arena,"A"));
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    Space multi={0};space_init(&multi);
    add(&multi,arena,"(p A)");add(&multi,arena,"(p B)");
    add(&multi,arena,"(exec 0 (, (p $x)) (O (- A) (+ $x)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&multi,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.raw_mm2_order=true});
    CHECK(agenda);run(agenda,1u);
    bool multiple=!space_contains_exact(&multi,parse(arena,"A")) &&
        space_contains_exact(&multi,parse(arena,"B")) &&
        space_contains_exact(&multi,parse(arena,"(p A)")) &&
        space_contains_exact(&multi,parse(arena,"(p B)"));
    cetta_space_agenda_free(agenda);space_free(&multi);
    bool correct=!observed[1][0] && !observed[1][1] &&
        !observed[0][0] && observed[0][1] && multiple;
    printf("Sink order: raw add-remove=%u remove-add=%u; explicit add-remove=%u remove-add=%u\n",
        observed[1][0],observed[1][1],observed[0][0],observed[0][1]);
    printf("Raw multi-row sink phase: %u\n",multiple);
    return correct?0:1;
}

/* Set capture contracts duplicate support rows. Its cursor coordinates must
 * still refer to the intended source occurrences when selecting an exec. */
static int set_capture_occurrence_controls(Arena *arena,Bindings *env) {
    CettaIndex decoded=0u;
    CHECK(agenda_natural_index(natural(arena,UINT64_MAX),&decoded) && decoded==UINT64_MAX);
    CHECK(!agenda_natural_index(atom_symbol(arena,"18446744073709551616"),&decoded));
    CHECK(!agenda_natural_index(atom_int(arena,-1),&decoded));
    bool self_blocked[2]={false}, unrelated_read[2]={false}, unrelated_take[2]={false};
    bool source_coordinates[2][2]={{false}};
    for(unsigned set=0u;set<2u;set++) for(unsigned which=0u;which<3u;which++) {
        Space space={0};space_init(&space);
        add(&space,arena,"Noise");add(&space,arena,"Noise");
        add(&space,arena,which==0u
            ? "(exec Self (I (read (exec Self $inputs $outputs))) (O (+ Wrong)))"
            : which==1u ? "(exec Probe (I (read Tail)) (O (+ Done)))"
            : "(exec Probe (I (take Tail)) (O (+ Done)))");
        if(which)add(&space,arena,"Tail");
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.set_support=!!set,.explicit_inputs=true});
        CHECK(agenda);
        SpaceReadToken before=space_read_token(&space);
        CettaSpaceAgendaStep last=CETTA_SPACE_AGENDA_PROGRESS;
        for(unsigned i=0u;i<10000u;i++) {
            last=cetta_space_agenda_step(agenda);
            if(which && agenda->phase==AGENDA_BUILD && agenda->validation_count) {
                Atom *dependency=agenda->validation[0].dependencies->expr.elems[0];
                CettaIndex source=UINT64_MAX;
                source_coordinates[set][which-1u]=
                    agenda_natural_index(dependency->expr.elems[2],&source) && source==3u;
            }
            if(last==CETTA_SPACE_AGENDA_COMMITTED || last==CETTA_SPACE_AGENDA_PENDING ||
               last==CETTA_SPACE_AGENDA_CLOSED)break;
        }
        if(which) {
            bool good=last==CETTA_SPACE_AGENDA_COMMITTED &&
                space_contains_exact(&space,parse(arena,"Tail"))==(which==1u) &&
                space_contains_exact(&space,parse(arena,"Done"));
            if(which==1u)unrelated_read[set]=good;else unrelated_take[set]=good;
        } else {
            self_blocked[set]=last==CETTA_SPACE_AGENDA_PENDING &&
                space_read_token_is_current(before) &&
                !space_contains_exact(&space,parse(arena,"Wrong"));
        }
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    printf("Captured occurrences: bag self=%u read=%u take=%u source=%u/%u; "
           "set self=%u read=%u take=%u source=%u/%u\n",
        self_blocked[0],unrelated_read[0],unrelated_take[0],
        source_coordinates[0][0],source_coordinates[0][1],
        self_blocked[1],unrelated_read[1],unrelated_take[1],
        source_coordinates[1][0],source_coordinates[1][1]);
    return self_blocked[0] && unrelated_read[0] && unrelated_take[0] &&
           self_blocked[1] && unrelated_read[1] && unrelated_take[1] &&
           source_coordinates[0][0] && source_coordinates[0][1] &&
           source_coordinates[1][0] && source_coordinates[1][1] ? 0 : 1;
}

typedef struct { uint64_t before,after; unsigned events,commits; } SetCaptureObservation;
static bool set_capture_observe(void *raw,const CettaSpaceAgendaEvent *event) {
    SetCaptureObservation *observation=raw;observation->events++;
    if(event->kind==CETTA_AGENDA_EVENT_COMMITTED)observation->commits++;
    return true;
}
static bool set_capture_permit(void *raw,const CettaSpaceAgendaEvent *event) {
    (void)raw;return event->kind==CETTA_AGENDA_EVENT_PRECOMMIT;
}
static bool set_capture_charge(void *raw,const CettaSpaceAgenda *agenda,
        CettaSpaceAgendaChargeSite site,unsigned kind,uint64_t units,bool after) {
    (void)agenda;(void)site;(void)kind;
    SetCaptureObservation *observation=raw;
    if(after)observation->after+=units;else observation->before+=units;
    return true;
}

static void set_capture_allocation_refusal(Arena *arena,Bindings *env) {
  for(unsigned recorded=0u;recorded<2u;recorded++) {
    Space space={0};space_init(&space);
    add(&space,arena,"Noise");add(&space,arena,"Noise");
    add(&space,arena,"(exec Probe (I (take Tail)) (O (+ Done)))");
    add(&space,arena,"Tail");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    SetCaptureObservation notes={0};CettaSpaceAgendaObservationScope observation={0};
    if(recorded) {
        CHECK(!cetta_space_agenda_observation_enter(&observation,agenda,
            set_capture_observe,set_capture_permit,set_capture_charge,&notes));
        cetta_space_agenda_free(agenda);space_free(&space);continue;
    }
    until_phase(agenda,"Capture");
    SpaceReadToken before=space_read_token(&space);
    CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
    CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,0u);
    for(unsigned i=0u;i<100u && !agenda->snapshot_origins.target_to_source.backing.blocks;i++)
        CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(agenda->capture_row_ready && agenda->capture_index==0u &&
        agenda->snapshot_origins.target_to_source.backing.blocks);
    CettaIndex target=agenda->capture_target;
    CettaSegmentedBlock *block=agenda->snapshot_origins.target_to_source.backing.blocks;
    CettaStorageCensus census={0};cetta_space_agenda_storage(agenda,&census);
    cetta_storage_finish(&census);
    bool counted=false;
    for(size_t i=0u;i<census.capacity;i++)
        if(census.slots[i].identity==block && census.slots[i].bytes==block->bytes)counted=true;
    CHECK(!census.incomplete && !census.overflow && counted);
    cetta_storage_census_free(&census);
    uint64_t probes=meter.total.counts[CETTA_COST_INDEX_VISIT];
    uint64_t captures=meter.total.counts[CETTA_COST_CAPTURE];
    unsigned refusals=agenda_test_refusals;
    agenda_test_refuse_calloc=true;
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(!agenda_test_refuse_calloc && agenda_test_refusals==refusals+1u &&
        agenda->capture_row_ready && agenda->capture_target==target && agenda->capture_index==0u &&
        agenda->snapshot_origins.target_to_source.backing.blocks==block &&
        space_read_token_is_current(before));
    CHECK(meter.total.counts[CETTA_COST_INDEX_VISIT]==probes+3u &&
        meter.total.counts[CETTA_COST_CAPTURE]==captures+1u);
    cetta_space_agenda_resume(agenda);
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(agenda->capture_row_ready && agenda->capture_target==target && agenda->capture_index==0u &&
        meter.total.counts[CETTA_COST_INDEX_VISIT]==probes+6u &&
        meter.total.counts[CETTA_COST_CAPTURE]==captures+2u);
    run(agenda,1u);
    CHECK(!space_contains_exact(&space,parse(arena,"Tail")) &&
        space_contains_exact(&space,parse(arena,"Done")));
    cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&meter);
    cetta_space_agenda_free(agenda);space_free(&space);
  }
    puts("PASS: captured source coordinates retain paid progress across allocation refusal, count probes and remain in the ownership census; set recording remains unadmitted");
}

static void set_capture_admission_refusal(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);add(&space,arena,"Token");
    add(&space,arena,"(exec Probe (I (take Token)) (O (+ Done)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    until_phase(agenda,"Capture");SpaceReadToken before=space_read_token(&space);
    for(unsigned i=0u;i<100u &&
        !cetta_segmented_array64_at(&agenda->snapshot_origins.target_to_source,0u);i++)
        CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(agenda->capture_row_ready && agenda->capture_index==0u &&
        !agenda->snapshot_origins.length && !space_length64(&agenda->snapshot));
    unsigned refusals=agenda_test_admission_refusals;
    agenda_test_refuse_admission=true;
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(!agenda_test_refuse_admission && agenda_test_admission_refusals==refusals+1u &&
        agenda->capture_row_ready && agenda->capture_index==0u &&
        !agenda->snapshot_origins.length && !space_length64(&agenda->snapshot) &&
        space_read_token_is_current(before));
    cetta_space_agenda_resume(agenda);
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(!agenda->capture_row_ready && agenda->capture_index==1u &&
        agenda->snapshot_origins.length==1u && space_length64(&agenda->snapshot)==1u);
    run(agenda,1u);
    CHECK(!space_contains_exact(&space,parse(arena,"Token")) &&
        space_contains_exact(&space,parse(arena,"Done")));
    cetta_space_agenda_free(agenda);space_free(&space);
    puts("PASS: refused snapshot admission keeps its prepared origin and resumes without publishing a partial capture");
}

static void set_capture_appended_duplicate(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    add(&space,arena,"(Fact $x)");
    add(&space,arena,"(exec Probe (I (take (Fact $z))) (O (+ Done)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    until_phase(agenda,"ValidateCapture");
    CHECK(agenda->row_count==1u && !agenda->retries);
    add(&space,arena,"(Fact $y)");
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_RETRY && agenda->retries==1u);
    run(agenda,1u);
    CHECK(!space_contains_exact(&space,parse(arena,"(Fact $x)")) &&
        !space_contains_exact(&space,parse(arena,"(Fact $y)")) &&
        space_contains_exact(&space,parse(arena,"Done")) && agenda->retries==1u);
    cetta_space_agenda_free(agenda);space_free(&space);
    puts("PASS: revalidation of an appended alpha-equivalent set member keeps its source representative and consumes the set member once");
}

static void set_capture_search_quanta(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    for(unsigned i=0u;i<16u;i++) {
        char fact[40];snprintf(fact,sizeof(fact),"(Noise %u)",i);add(&space,arena,fact);
    }
    add(&space,arena,"(exec Probe (I) (O (+ Done)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    until_phase(agenda,"Capture");
    for(unsigned i=0u;i<10000u && !(agenda->capture_index==16u && agenda->capture_row_ready);i++)
        CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(agenda->capture_index==16u && agenda->capture_row_ready && !agenda->capture_probe);
    unsigned visits=0u;
    while(!agenda->capture_search_done) {
        CettaIndex previous=agenda->capture_probe;
        CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
        CHECK(agenda->capture_index==16u &&
            (agenda->capture_probe==previous || agenda->capture_probe==previous+1u));
        if(agenda->capture_probe==previous+1u)visits++;
    }
    CHECK(visits==16u && agenda->capture_probe==16u && agenda->capture_target==16u);
    run(agenda,1u);CHECK(space_contains_exact(&space,parse(arena,"Done")));
    cetta_space_agenda_free(agenda);space_free(&space);
    puts("PASS: set capture visits at most one existing member per retained search step");
}

static void set_capture_changed_source(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);add(&space,arena,"Token");
    add(&space,arena,"(exec Probe (I (take Token)) (O (+ Done)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    until_phase(agenda,"Capture");
    for(unsigned i=0u;i<100u && !agenda->snapshot_origins.target_to_source.backing.blocks;i++)
        CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(agenda->capture_row_ready && agenda->snapshot_origins.target_to_source.backing.blocks);
    CettaSegmentedBlock *held=agenda->snapshot_origins.target_to_source.backing.blocks;
    add(&space,arena,"Extra");
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_RETRY &&
        !agenda->capture_row_ready && !agenda->capture_search_done &&
        agenda->snapshot_origins.target_to_source.backing.blocks==held &&
        !agenda->snapshot_origins.length && !agenda->capture_retiring && agenda->capture_index==0u);
    run(agenda,1u);
    CHECK(space_contains_exact(&space,parse(arena,"Extra")) &&
        space_contains_exact(&space,parse(arena,"Done")) &&
        !space_contains_exact(&space,parse(arena,"Token")) &&
        capture_origins_empty(&agenda->snapshot_origins) &&
        capture_origins_empty(&agenda->current_origins));
    cetta_space_agenda_free(agenda);space_free(&space);
    puts("PASS: a changed source restarts set capture without publishing its partial coordinate map");
}

static size_t capture_origin_blocks(const AgendaCaptureOrigins *origins) {
    size_t count=0u;
    for(const CettaSegmentedBlock *block=origins->target_to_source.backing.blocks;
        block;block=block->next)count++;
    return count;
}

static void set_capture_retirement_steps(Arena *arena,Bindings *env) {
    for(unsigned cancel=0u;cancel<2u;cancel++) {
        Space space={0};space_init(&space);add(&space,arena,"Token");
        add(&space,arena,"(exec Probe (I (take Token)) (O (+ Done)))");
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
        bool committed=false;
        for(unsigned i=0u;i<10000u && !committed;i++) {
            CettaSpaceAgendaStep step=cetta_space_agenda_step(agenda);
            CHECK(step!=CETTA_SPACE_AGENDA_PENDING && step!=CETTA_SPACE_AGENDA_CLOSED);
            committed=step==CETTA_SPACE_AGENDA_COMMITTED;
        }
        CHECK(committed && agenda->phase==AGENDA_SELECT && agenda->capture_retiring);
        size_t blocks=capture_origin_blocks(&agenda->snapshot_origins)+
            capture_origin_blocks(&agenda->current_origins);
        CHECK(blocks>2u);
        SpaceReadToken before=space_read_token(&space);
        Atom *receipt=cetta_space_agenda_receipt(agenda);CHECK(receipt);
        CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
        CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,0u);
        size_t retired=0u;
        while(agenda->capture_retiring && (!cancel || retired<blocks/2u)) {
            size_t prior=capture_origin_blocks(&agenda->snapshot_origins)+
                capture_origin_blocks(&agenda->current_origins);
            uint64_t charges=meter.total.counts[CETTA_COST_CAPTURE];
            CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
            size_t remaining=capture_origin_blocks(&agenda->snapshot_origins)+
                capture_origin_blocks(&agenda->current_origins);
            CHECK(prior==remaining+1u &&
                meter.total.counts[CETTA_COST_CAPTURE]==charges+1u &&
                space_read_token_is_current(before) && cetta_space_agenda_receipt(agenda)==receipt);
            retired++;
        }
        if(!cancel) {
            CHECK(retired==blocks && !agenda->capture_retiring &&
                capture_origins_empty(&agenda->snapshot_origins) &&
                capture_origins_empty(&agenda->current_origins));
            run(agenda,0u);
        } else CHECK(retired==blocks/2u && agenda->capture_retiring);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&meter);
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    puts("PASS: capture backing retires one charged block per step and can be destroyed after partial retirement");
}

typedef struct { unsigned calls; int ranks[2]; } SetCaptureRanking;
static void *set_capture_rank(void *raw,Atom *exec,const Bindings *bindings,Atom *priority) {
    (void)exec;(void)bindings;(void)priority;
    SetCaptureRanking *ranking=raw;
    return ranking->calls<2u ? &ranking->ranks[ranking->calls++] : NULL;
}
static bool set_capture_rank_before(void *raw,const void *left,const void *right) {
    (void)raw;return *(const int *)left<*(const int *)right;
}

static void set_capture_selected_equivalence(Arena *arena,Bindings *env) {
    const char *first="(exec Same (I (read (val $x))) (O (+ (result $x))))";
    const char *second="(exec Same (I (read (val $y))) (O (+ (result $y))))";
    Space space={0};space_init(&space);
    add(&space,arena,"(val A)");add(&space,arena,first);add(&space,arena,second);
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
    CettaSelectionAutomaton schedule;CHECK(cetta_selection_automaton_ratio(1u,&schedule));
    SetCaptureRanking ranking={.ranks={1,0}};
    CHECK(cetta_space_agenda_control(agenda,&schedule,set_capture_rank,
        set_capture_rank_before,&ranking));
    bool observed=false,committed=false;
    for(unsigned i=0u;i<10000u;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_step(agenda);
        if(agenda->phase==AGENDA_BUILD)
            observed=agenda->selected_index==2u && agenda->current_origins.selected_ready &&
                agenda->current_origins.selected_target==1u;
        if(step==CETTA_SPACE_AGENDA_COMMITTED){committed=true;break;}
        CHECK(step!=CETTA_SPACE_AGENDA_PENDING && step!=CETTA_SPACE_AGENDA_CLOSED);
    }
    CHECK(observed && committed && ranking.calls==2u &&
        space_contains_exact(&space,parse(arena,"(result A)")) &&
        !space_contains_exact(&space,parse(arena,first)) &&
        !space_contains_exact(&space,parse(arena,second)));
    cetta_space_agenda_free(agenda);space_free(&space);
    puts("PASS: a selected alpha-equivalent duplicate maps to its earlier captured set member");
}

/* Compare set support with the existing captured-environment behavior when
 * only one of two alpha-shaped directives uses a held outer binding. */
static int set_capture_bound_environment(Arena *arena,Bindings *env) {
    bool survives[2][2]={{false}},expected_answer[2][2]={{false}};
    for(unsigned set=0u;set<2u;set++) for(unsigned chosen=0u;chosen<2u;chosen++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(token A)");add(&space,arena,"(token B)");
        Atom *first=parse(arena,"(exec Same (I (read (token $outer))) (O (+ (kept $outer))))");
        Atom *second=parse(arena,"(exec Same (I (read (token $other))) (O (+ (kept $other))))");
        Atom *outer=first->expr.elems[2]->expr.elems[1]->expr.elems[1]->expr.elems[1];
        Bindings captured;CHECK(bindings_clone(&captured,env));
        CHECK(bindings_add_var(&captured,outer,atom_symbol(arena,"B")));
        CHECK(space_admit_atom(&space,arena,first));CHECK(space_admit_atom(&space,arena,second));
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,&captured,
            (CettaSpaceAgendaContract){.set_support=!!set,.explicit_inputs=true});CHECK(agenda);
        bindings_free(&captured);
        CettaSelectionAutomaton schedule;CHECK(cetta_selection_automaton_ratio(1u,&schedule));
        SetCaptureRanking ranking={.ranks={chosen?1:0,chosen?0:1}};
        CHECK(cetta_space_agenda_control(agenda,&schedule,set_capture_rank,
            set_capture_rank_before,&ranking));
        bool committed=false;
        for(unsigned i=0u;i<10000u;i++) {
            CettaSpaceAgendaStep step=cetta_space_agenda_step(agenda);
            if(step==CETTA_SPACE_AGENDA_COMMITTED){committed=true;break;}
            if(step==CETTA_SPACE_AGENDA_PENDING || step==CETTA_SPACE_AGENDA_CLOSED)break;
        }
        for(CettaIndex i=0u;committed && i<space_length64(&space);i++) {
            Atom *candidate=space_get_at64(&space,i);
            if (shaped(candidate,agenda->exec,3u) && atom_eq(candidate,chosen?first:second))
                survives[set][chosen]=true;
        }
        expected_answer[set][chosen]=committed &&
            space_contains_exact(&space,parse(arena,chosen?"(kept A)":"(kept B)"));
        if(!survives[set][chosen] || !expected_answer[set][chosen]) {
            fprintf(stderr,"outer-binding case set=%u chosen=%u committed=%u:\n",set,chosen,committed);
            for(CettaIndex i=0u;i<space_length64(&space);i++) {
                atom_print(space_get_at64(&space,i),stderr);fputc('\n',stderr);
            }
        }
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    printf("Captured outer binding: bag survivors=%u/%u answers=%u/%u; "
           "set survivors=%u/%u answers=%u/%u\n",
        survives[0][0],survives[0][1],expected_answer[0][0],expected_answer[0][1],
        survives[1][0],survives[1][1],expected_answer[1][0],expected_answer[1][1]);
    return survives[0][0] && survives[0][1] && expected_answer[0][0] &&
        expected_answer[0][1] && survives[1][0] && survives[1][1] &&
        expected_answer[1][0] && expected_answer[1][1] ? 0 : 1;
}

/* Diagnostic only: raw MM2 uses a different source interpretation, so this
 * does not decide whether its open directives should capture the caller. */
static void raw_capture_bound_inspect(Arena *arena,Bindings *env) {
    for(unsigned chosen=0u;chosen<2u;chosen++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(token A)");add(&space,arena,"(token B)");
        Atom *first=parse(arena,"(exec Same (, (token $outer)) (O (+ (kept $outer))))");
        Atom *second=parse(arena,"(exec Same (, (token $other)) (O (+ (kept $other))))");
        Atom *outer=first->expr.elems[2]->expr.elems[1]->expr.elems[1];
        Bindings captured;CHECK(bindings_clone(&captured,env));
        CHECK(bindings_add_var(&captured,outer,atom_symbol(arena,"B")));
        CHECK(space_admit_atom(&space,arena,first));CHECK(space_admit_atom(&space,arena,second));
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,&captured,
            (CettaSpaceAgendaContract){.set_support=true,.raw_mm2_order=true});CHECK(agenda);
        bindings_free(&captured);
        CettaSelectionAutomaton schedule;CHECK(cetta_selection_automaton_ratio(1u,&schedule));
        SetCaptureRanking ranking={.ranks={chosen?1:0,chosen?0:1}};
        CHECK(cetta_space_agenda_control(agenda,&schedule,set_capture_rank,
            set_capture_rank_before,&ranking));
        bool committed=false;
        for(unsigned i=0u;i<10000u;i++) {
            CettaSpaceAgendaStep step=cetta_space_agenda_step(agenda);
            if(step==CETTA_SPACE_AGENDA_COMMITTED){committed=true;break;}
            if(step==CETTA_SPACE_AGENDA_PENDING || step==CETTA_SPACE_AGENDA_CLOSED)break;
        }
        bool survivor=false;
        for(CettaIndex i=0u;committed && i<space_length64(&space);i++) {
            Atom *candidate=space_get_at64(&space,i);
            if (shaped(candidate,agenda->exec,3u) && atom_eq(candidate,chosen?first:second))
                survivor=true;
        }
        printf("Raw MM2 held context: chosen=%u committed=%u other=%u A=%u B=%u\n",
            chosen,committed,survivor,
            space_contains_exact(&space,parse(arena,"(kept A)")),
            space_contains_exact(&space,parse(arena,"(kept B)")));
        cetta_space_agenda_free(agenda);space_free(&space);
    }
}

static void set_capture_stored_data_scope(Arena *arena,Bindings *env) {
    for(unsigned malformed=0u;malformed<2u;malformed++) {
        Space space={0};space_init(&space);
        Atom *first=parse(arena,malformed ?
            "(exec NotExecutable (Unknown $outer) (O))" : "(p $outer)");
        Atom *second=parse(arena,malformed ?
            "(exec NotExecutable (Unknown $other) (O))" : "(p $other)");
        Atom *variable=malformed ? first->expr.elems[2]->expr.elems[1] : first->expr.elems[1];
        Bindings captured;CHECK(bindings_clone(&captured,env));
        CHECK(bindings_add_var(&captured,variable,atom_symbol(arena,"B")));
        /* The valid probe precedes unknown exec-shaped data in the agenda. */
        if(malformed)add(&space,arena,
            "(exec Probe (I (all (exec NotExecutable $in $out) $rows)) (O (+ Done)))");
        CHECK(space_admit_atom(&space,arena,first));CHECK(space_admit_atom(&space,arena,second));
        if(!malformed)add(&space,arena,"(exec Probe (I (all (p $z) $rows)) (O (+ Done)))");
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,&captured,
            (CettaSpaceAgendaContract){.set_support=true,.explicit_inputs=true});CHECK(agenda);
        bindings_free(&captured);
        until_phase(agenda,"Build");
        CHECK(agenda->validation_count==1u);
        Atom *witnesses=agenda->validation[0].witnesses;
        CHECK(witnesses && witnesses->kind==ATOM_EXPR && witnesses->expr.len==1u);
        Atom *rows=witnesses->expr.elems[0];
        CHECK(rows && rows->kind==ATOM_EXPR && rows->expr.len==1u);
        Atom *fact=rows->expr.elems[0];
        CHECK(fact && fact->kind==ATOM_EXPR && fact->expr.len==(malformed?4u:2u));
        Atom *fresh=malformed ? fact->expr.elems[2]->expr.elems[1] : fact->expr.elems[1];
        CHECK(fresh->kind==ATOM_VAR);
        if(malformed) {
            CHECK(until_pending(agenda)==1u);
            CHECK(agenda->pending && !strcmp(agenda->pending,"UninterpretedExec"));
        } else run(agenda,1u);
        CHECK(space_contains_exact(&space,parse(arena,"Done")));
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    puts("PASS: captured outer bindings do not capture variables inside stored data or uninterpreted exec rows");
}

/* A legacy removal's exact-then-unique-alpha decision depends on whether a
 * kept occurrence is still in its candidate store. */
static int legacy_removal_resolution_controls(Arena *arena) {
    Atom *x=parse(arena,"(p $x)"),*y=parse(arena,"(p $y)"),*w=parse(arena,"(p $w)");
    Space full={0},excluded={0};space_init(&full);space_init(&excluded);
    CHECK(space_admit_atom(&full,arena,x));CHECK(space_admit_atom(&full,arena,y));
    CHECK(space_admit_atom(&excluded,arena,y));
    CettaMatchCompletion global=space_remove_checked(&full,w);
    CettaMatchCompletion filtered=space_remove_checked(&excluded,w);
    bool ambiguous=global==CETTA_MATCH_REFUTED && space_length64(&full)==2u &&
        filtered==CETTA_MATCH_MATCHED && !space_length64(&excluded);
    space_free(&full);space_free(&excluded);

    space_init(&full);space_init(&excluded);
    CHECK(space_admit_atom(&full,arena,x));CHECK(space_admit_atom(&full,arena,y));
    CHECK(space_admit_atom(&excluded,arena,y));
    global=space_remove_checked(&full,x);
    filtered=space_remove_checked(&excluded,x);
    bool precedence=global==CETTA_MATCH_MATCHED && space_length64(&full)==1u &&
        atom_eq(space_get_at64(&full,0u),y) &&
        filtered==CETTA_MATCH_MATCHED && !space_length64(&excluded);
    space_free(&full);space_free(&excluded);
    printf("Legacy removal resolution: global ambiguity=%u exact precedence=%u\n",ambiguous,precedence);
    return ambiguous && precedence ? 0 : 1;
}

/* A failed removal preparation retains the exact sink and already-staged
 * outputs. A missing atom is a completed no-op, not the same failure. */
static void legacy_removal_refusal_controls(Arena *arena,Bindings *env) {
    for(unsigned recorded=0u;recorded<2u;recorded++)for(unsigned mode=0u;mode<2u;mode++) {
        Space s={0};space_init(&s);add(&s,arena,"Token");
        add(&s,arena,mode ?
            "(exec Query (I (all Token $rows)) (O (+ Mark) (- Missing) (+ Done)))" :
            "(exec Query (I (all Token $rows)) (O (+ Mark) (- Token) (+ Done)))");
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
        SetCaptureObservation notes={0};CettaSpaceAgendaObservationScope observation={0};
        if(recorded)CHECK(cetta_space_agenda_observation_enter(&observation,a,
            set_capture_observe,set_capture_permit,set_capture_charge,&notes));
        until_phase(a,"Sink");
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
        CHECK(space_contains_exact(&a->replacement,parse(arena,"Mark")));
        SpaceReadToken source=space_read_token(&s);
        CettaIndex position=a->sink_index;
        CettaCount staged=space_length64(&a->replacement);
        agenda_test_refuse_removal=true;agenda_test_removal_refusals=0u;
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PENDING);
        CHECK(agenda_test_removal_refusals==1u && a->sink_index==position &&
            space_read_token_is_current(source) && space_length64(&a->replacement)==staged &&
            space_contains_exact(&a->replacement,parse(arena,"Mark")));
        run(a,1u);
        CHECK(space_contains_exact(&s,parse(arena,"Mark")) &&
            space_contains_exact(&s,parse(arena,"Done")) &&
            space_contains_exact(&s,parse(arena,"Token"))==(mode!=0u) &&
            space_length64(&s)==(mode?3u:2u));
        if(recorded) {
            CHECK(notes.commits==1u);
            cetta_space_agenda_observation_leave(&observation);
        }
        cetta_space_agenda_free(a);space_free(&s);
    }
    puts("PASS: incomplete legacy removal retains its sink and staged prefix for retry");
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

static void kept_read_identification(Arena *arena,Bindings *env) {
    const char *rules[]={
        "(exec Claim (I (read Token) (take Token)) (O (+ Done)))",
        "(exec Claim (I (take Token) (read Token)) (O (+ Done)))"};
    for(size_t order=0u;order<2u;order++) {
        Space s={0};space_init(&s);add(&s,arena,"Token");add(&s,arena,rules[order]);
        SpaceReadToken before=space_read_token(&s);
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
        CHECK(until_pending(a)==0u);
        CHECK(space_read_token_is_current(before));
        CHECK(!space_contains_exact(&s,parse(arena,"Done")));
        /* Equal values have different occurrences. A later second token
         * enables the retained rule without consuming its kept witness. */
        add(&s,arena,"Token");run(a,1u);
        CHECK(space_length64(&s)==2u);
        CHECK(space_contains_exact(&s,parse(arena,"Token")));
        CHECK(space_contains_exact(&s,parse(arena,"Done")));
        cetta_space_agenda_free(a);space_free(&s);
    }
    const char *queries[]={
        "(exec Query (I (all Token $seen) (take Token)) (O (+ (Seen $seen))))",
        "(exec Query (I (take Token) (all Token $seen)) (O (+ (Seen $seen))))"};
    for(size_t order=0u;order<2u;order++) {
        Space s={0};space_init(&s);add(&s,arena,"Token");add(&s,arena,queries[order]);
        CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
        run(a,1u);
        CHECK(space_length64(&s)==1u);
        CHECK(space_contains_exact(&s,parse(arena,"(Seen (Token))")));
        cetta_space_agenda_free(a);space_free(&s);
    }
    Space s={0};space_init(&s);
    add(&s,arena,"(tok 1)");add(&s,arena,"(tok 2)");add(&s,arena,"(tok 3)");
    add(&s,arena,"(exec A (I (read (tok $x)) (take (tok $y))) (O (+ (took $y))))");
    add(&s,arena,"(exec B (I (read (tok $x)) (take (tok $y))) (O (+ (took $y))))");
    add(&s,arena,"(exec C (I (read (tok $x)) (take (tok $y))) (O (+ (took $y))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    CHECK(until_pending(a)==2u);
    CHECK(space_length64(&s)==4u && space_contains_exact(&s,parse(arena,"(tok 1)")));
    CHECK(space_contains_exact(&s,parse(arena,"(took 2)")));
    CHECK(space_contains_exact(&s,parse(arena,"(took 3)")));
    cetta_space_agenda_free(a);space_free(&s);

    space_init(&s);
    add(&s,arena,"(exec Self (I (read (exec Self $inputs $outputs))) (O (+ Wrong)))");
    a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    SpaceReadToken before=space_read_token(&s);
    CHECK(until_pending(a)==0u && space_read_token_is_current(before));
    CHECK(!space_contains_exact(&s,parse(arena,"Wrong")));
    cetta_space_agenda_free(a);space_free(&s);
    puts("PASS: kept reads and takes use distinct occurrences in both orders; predicate queries retain pre-state meaning and reserve rules keep one token");
}

/* A full pre-state query does not keep its members; an ordinary read does.
 * Legacy removal must not erase the witness selected by a kept read. */
static bool kept_read_legacy_removal(Arena *arena,Bindings *env) {
    bool kept_witness, duplicate_survives, later_witness;
    Space s={0};space_init(&s);
    add(&s,arena,"(token A)");
    add(&s,arena,"(exec Query (I (all (token $x) $rows)) (O (- (token A)) (+ Queried)))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    run(a,1u);
    CHECK(!space_contains_exact(&s,parse(arena,"(token A)")) &&
        space_contains_exact(&s,parse(arena,"Queried")));
    cetta_space_agenda_free(a);space_free(&s);

    space_init(&s);
    add(&s,arena,"(token A)");
    add(&s,arena,"(exec Keep (I (read (token $x))) (O (- (token $x)) (+ Kept)))");
    a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    bool stopped=false;
    for(unsigned i=0u;i<10000u;i++) {
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        if(step==CETTA_SPACE_AGENDA_PENDING || step==CETTA_SPACE_AGENDA_CLOSED) {
            stopped=true;break;
        }
    }
    kept_witness=stopped && space_contains_exact(&s,parse(arena,"(token A)")) &&
        !space_contains_exact(&s,parse(arena,"Kept"));
    cetta_space_agenda_free(a);space_free(&s);

    /* Equal payloads still give two distinct occurrences: removal can take
     * the other one while the selected read witness survives. */
    space_init(&s);
    add(&s,arena,"Token");add(&s,arena,"Token");
    add(&s,arena,"(exec Reserve (I (read Token)) (O (- Token) (+ Kept)))");
    a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    run(a,1u);
    duplicate_survives=space_length64(&s)==2u &&
        space_contains_exact(&s,parse(arena,"Token")) &&
        space_contains_exact(&s,parse(arena,"Kept"));
    cetta_space_agenda_free(a);space_free(&s);

    /* If the first matched witness conflicts, a later independent witness
     * must remain available to the rule rather than losing the entire call. */
    space_init(&s);
    add(&s,arena,"(token A)");add(&s,arena,"(token B)");
    add(&s,arena,"(exec Choose (I (read (token $x))) (O (- (token A)) (+ (Kept $x))))");
    a=cetta_space_agenda_new(&s,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    run(a,1u);
    later_witness=!space_contains_exact(&s,parse(arena,"(token A)")) &&
        space_contains_exact(&s,parse(arena,"(token B)")) &&
        space_contains_exact(&s,parse(arena,"(Kept B)"));
    cetta_space_agenda_free(a);space_free(&s);

    /* Exercise all three even when an earlier example fails. Equal token
     * values establish multiplicity here; a stable occurrence certificate
     * is additionally needed to distinguish which physical token survived. */
    if(!kept_witness || !duplicate_survives || !later_witness)
        fprintf(stderr,"legacy removals: kept=%u duplicate=%u later=%u\n",
            kept_witness,duplicate_survives,later_witness);
    return kept_witness && duplicate_survives && later_witness;
}

static size_t count_exact(Space *s,Arena *a,const char *text);
static unsigned removal_comparisons,removal_incomplete;
static CettaMatchCompletion removal_equal_control(Atom *left,Atom *right) {
    removal_comparisons++;
    if(removal_incomplete){removal_incomplete--;return CETTA_MATCH_INCOMPLETE;}
    return atom_eq(left,right)?CETTA_MATCH_MATCHED:CETTA_MATCH_REFUTED;
}
static CettaMatchCompletion removal_alpha_control(Atom *left,Atom *right) {
    removal_comparisons++;
    return atom_alpha_eq_checked(left,right);
}
static SpaceRemovalSearchStep resolve_removal_control(SpaceRemovalSearch *search,Atom *body,
        const uint8_t *kept,CettaCount count) {
    for(unsigned i=0u;i<10000u;i++) {
        unsigned before=removal_comparisons;
        SpaceRemovalSearchStep step=space_removal_search_step(search,body,
            removal_equal_control,removal_alpha_control,kept,count);
        CHECK(removal_comparisons-before<=1u);
        if(step!=SPACE_REMOVAL_PROGRESS)return step;
    }
    CHECK(false);return SPACE_REMOVAL_INCOMPLETE;
}
static void removal_search_controls(Arena *arena) {
    Space space={0};space_init(&space);
    Atom *token=parse(arena,"Token");
    add(&space,arena,"Token");add(&space,arena,"Token");add(&space,arena,"Other");
    uint8_t kept[]={1u,0u,0u};
    SpaceRemovalSearch search;
    CHECK(space_removal_search_init(&space,&search));
    SpaceReadToken before=space_read_token(&space);
    CHECK(resolve_removal_control(&search,token,kept,3u)==SPACE_REMOVAL_SELECTED && search.selected==1u);
    CHECK(space_read_token_is_current(before));
    CHECK(space_remove_occurrence_checked(&space,search.read,search.selected)==CETTA_MATCH_MATCHED);
    CHECK(space_length64(&space)==2u && atom_eq(space_get_at64(&space,0u),token));
    CHECK(space_remove_occurrence_checked(&space,before,0u)==CETTA_MATCH_REFUTED);
    CHECK(space_length64(&space)==2u);
    CHECK(space_removal_search_init(&space,&search));
    removal_incomplete=1u;
    CHECK(space_removal_search_step(&search,token,removal_equal_control,removal_alpha_control,
        kept,2u)==SPACE_REMOVAL_INCOMPLETE && search.next==0u && !search.exact_seen);
    CHECK(resolve_removal_control(&search,token,kept,2u)==SPACE_REMOVAL_PROTECTED);
    CHECK(space_removal_search_init(&space,&search));
    add(&space,arena,"New");
    CHECK(resolve_removal_control(&search,token,NULL,0u)==SPACE_REMOVAL_INVALIDATED);
    space_free(&space);

    /* Protection does not turn two global alpha matches into a unique one. */
    space_init(&space);add(&space,arena,"(edge $a $a)");add(&space,arena,"(edge $b $b)");
    Atom *query=parse(arena,"(edge $c $c)");
    CHECK(space_removal_search_init(&space,&search));
    CHECK(resolve_removal_control(&search,query,kept,2u)==SPACE_REMOVAL_UNMATCHED);
    /* An exact protected occurrence still beats an eligible alpha match. */
    Atom *exact=space_get_at64(&space,0u);
    CHECK(space_removal_search_init(&space,&search));
    CHECK(resolve_removal_control(&search,exact,kept,2u)==SPACE_REMOVAL_PROTECTED);
    space_free(&space);
    space_init(&space);add(&space,arena,"(edge $a $a)");
    CHECK(space_removal_search_init(&space,&search));
    CHECK(resolve_removal_control(&search,query,kept,1u)==SPACE_REMOVAL_PROTECTED);
    CHECK(space_removal_search_init(&space,&search));
    CHECK(resolve_removal_control(&search,parse(arena,"Missing"),kept,1u)==SPACE_REMOVAL_UNMATCHED);
    space_free(&space);
}

static void kept_removal_preparation_controls(Arena *arena,Bindings *env) {
    const char *rules[]={
        "(exec Keep (I (read Token)) (O (- Token) (+ Token) (+ Wrong)))",
        "(exec Keep (I (read Token)) (O (+ Token) (- Token) (+ Done)))",
        "(exec Keep (I (read Token)) (O (+ Mark) (- Token) (- Token) (+ Done)))",
        "(exec Keep (I (read Token)) (O (+ Mark) (- Token) (- Token) (- Token) (+ Token) (+ Wrong)))"};
    for(unsigned mode=0u;mode<4u;mode++)for(unsigned recorded=0u;recorded<2u;recorded++) {
        Space space={0};space_init(&space);add(&space,arena,"Token");
        if(mode>=2u){add(&space,arena,"Token");add(&space,arena,"Token");}
        add(&space,arena,rules[mode]);
        SpaceReadToken before=space_read_token(&space);
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(agenda);
        SetCaptureObservation notes={0};CettaSpaceAgendaObservationScope observation={0};
        if(recorded)CHECK(cetta_space_agenda_observation_enter(&observation,agenda,
            set_capture_observe,set_capture_permit,set_capture_charge,&notes));
        bool accepted=mode==1u || mode==2u;
        if(accepted) {
            until_phase(agenda,"Commit");
            CHECK(space_read_token_is_current(before));
            size_t kept_count=0u;
            for(size_t i=0u;i<agenda->replacement_kept_length;i++)kept_count+=agenda->replacement_kept[i]!=0u;
            CHECK(kept_count==1u);
            CHECK(!space_contains_exact(&space,parse(arena,"Done")));
            run(agenda,1u);
            CHECK(space_contains_exact(&space,parse(arena,"Done")) &&
                space_contains_exact(&space,parse(arena,"Token")));
            CHECK(space_length64(&space)==(mode==2u?3u:2u));
        } else {
            CHECK(until_pending(agenda)==0u);
            CHECK(space_read_token_is_current(before));
            CHECK(!space_contains_exact(&space,parse(arena,"Mark")) &&
                !space_contains_exact(&space,parse(arena,"Wrong")));
        }
        if(recorded) {
            CHECK(notes.commits==(accepted?1u:0u));
            cetta_space_agenda_observation_leave(&observation);
        }
        cetta_space_agenda_free(agenda);space_free(&space);
    }
}

static void kept_removal_validation_controls(Arena *arena,Bindings *env) {
    for(unsigned kind=0u;kind<3u;kind++) {
        Space space={0};space_init(&space);space.kind=(SpaceKind)kind;
        add(&space,arena,"(token A)");add(&space,arena,"(token B)");add(&space,arena,"(token B)");
        add(&space,arena,"(exec R (I (read (token $x))) (O (- (token $x)) (+ (Kept $x))))");
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(agenda);
        run(agenda,1u);
        CHECK(space.kind==(SpaceKind)kind && space_length64(&space)==3u &&
            count_exact(&space,arena,"(token B)")==1u &&
            space_contains_exact(&space,parse(arena,"(Kept B)")) &&
            !space_contains_exact(&space,parse(arena,"(Kept A)")));
        cetta_space_agenda_free(agenda);space_free(&space);
    }
    Space space={0};space_init(&space);add(&space,arena,"Token");add(&space,arena,"Other");
    add(&space,arena,"(exec R (I (all Token $rows)) (O (+ Mark) (- Token) (+ Done)))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(agenda);
    until_phase(agenda,"Sink");
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    SpaceReadToken before=space_read_token(&space);
    CettaNativeCostLedger meter;cetta_native_cost_init(&meter);
    CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&meter,0u);
    agenda_test_refuse_occurrence=true;
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PENDING);
    CHECK(agenda->removal_ready && agenda->removal.finished && agenda->removal.next<agenda->removal.end);
    uint64_t probes=meter.total.counts[CETTA_COST_INDEX_VISIT];
    CHECK(probes==1u && space_read_token_is_current(before));
    CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(meter.total.counts[CETTA_COST_INDEX_VISIT]==probes && !agenda->removal_ready);
    cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&meter);
    run(agenda,1u);
    CHECK(count_exact(&space,arena,"Mark")==1u && count_exact(&space,arena,"Done")==1u &&
        !space_contains_exact(&space,parse(arena,"Token")));
    cetta_space_agenda_free(agenda);space_free(&space);
}

static void kept_removal_interruption_controls(Arena *arena,Bindings *env) {
    for(unsigned boundary=0u;boundary<4u;boundary++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(token A)");add(&space,arena,"(token B)");add(&space,arena,"(token B)");
        add(&space,arena,"(exec R (I (read (token $x))) (O (+ Mark) (- (token $x)) (+ (Kept $x))))");
        CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(agenda);
        if(boundary==1u) {
            for(unsigned i=0u;;i++) {
                CHECK(i<10000u);
                if(agenda->phase==AGENDA_QUALIFY_BUILD && agenda->validating)break;
                CettaSpaceAgendaStep step=cetta_space_agenda_step(agenda);
                CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
            }
        } else until_phase(agenda,boundary==2u?"QualifySink":"QualifyBuild");
        SpaceReadToken before=space_read_token(&space);
        if(boundary<2u) {
            add(&space,arena,"Unrelated");
            run(agenda,1u);
            CHECK(space_contains_exact(&space,parse(arena,"Unrelated")) &&
                count_exact(&space,arena,"Mark")==1u &&
                space_contains_exact(&space,parse(arena,"(Kept B)")) && space_length64(&space)==5u);
        } else if(boundary==2u) {
            CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PROGRESS);
            CHECK(space_contains_exact(&agenda->replacement,parse(arena,"Mark")));
            CHECK(space_read_token_is_current(before) && !space_contains_exact(&space,parse(arena,"Mark")));
        } else {
            agenda_test_refuse_admission=true;
            CHECK(cetta_space_agenda_step(agenda)==CETTA_SPACE_AGENDA_PENDING);
            CHECK(space_read_token_is_current(before) && space_length64(&agenda->replacement)==0u &&
                agenda->replacement_kept_length==0u);
            run(agenda,1u);
            CHECK(count_exact(&space,arena,"Mark")==1u &&
                space_contains_exact(&space,parse(arena,"(Kept B)")));
        }
        cetta_space_agenda_free(agenda);
        if(boundary==2u)CHECK(space_read_token_is_current(before));
        space_free(&space);
    }
    /* Freshly matched variables do not become exact occurrences merely
     * because their original spelling appeared in the captured store. */
    Space space={0};space_init(&space);
    Atom *first=parse(arena,"(p $x)");CHECK(space_admit_atom(&space,arena,first));
    add(&space,arena,"(p $y)");
    add(&space,arena,"(exec R (I (read (p $a))) (O (- (p $a)) (+ (Seen $a $a))))");
    CettaSpaceAgenda *agenda=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(agenda);
    run(agenda,1u);
    bool first_survives=false;
    for(CettaIndex i=0u;i<space_length64(&space);i++)
        first_survives=first_survives || atom_eq(space_get_at64(&space,i),first);
    /* The indexed ground-membership API deliberately rejects open queries. */
    CHECK(space_length64(&space)==3u && first_survives);
    Atom *seen=NULL;
    for(CettaIndex i=0u;i<space_length64(&space);i++) {
        Atom *row=space_get_at64(&space,i);
        if(shaped(row,symbol_intern_cstr(g_symbols,"Seen"),2u))seen=row;
    }
    CHECK(seen && seen->expr.elems[1]->kind==ATOM_VAR && seen->expr.elems[2]->kind==ATOM_VAR &&
        var_id_eq(seen->expr.elems[1]->var_id,seen->expr.elems[2]->var_id) &&
        !var_id_eq(seen->expr.elems[1]->var_id,first->expr.elems[1]->var_id));
    cetta_space_agenda_free(agenda);space_free(&space);
}

static void predicate_query_context(Arena *arena,Bindings *env) {
    for(unsigned refined=0u;refined<2u;refined++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(Seed Captured)");add(&space,arena,"(Template $x $x)");
        add(&space,arena,refined ?
            "(exec Query (I (take (Seed $x)) (all (Template Refined $y) $rows)) (O (+ (Result $rows))))" :
            "(exec Query (I (take (Seed $x)) (all (Template $y $z) $rows)) (O (+ (Result $rows))))");
        CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
            (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
        run(a,1u);
        Atom *result=space_get_at64(&space,space_length64(&space)-1u);
        CHECK(shaped(result,symbol_intern_cstr(g_symbols,"Result"),1u));
        Atom *rows=result->expr.elems[1];CHECK(rows->kind==ATOM_EXPR && rows->expr.len==1u);
        Atom *row=rows->expr.elems[0];CHECK(shaped(row,symbol_intern_cstr(g_symbols,"Template"),2u));
        if(refined)CHECK(atom_eq(row,parse(arena,"(Template Refined Refined)")));
        else {
            CHECK(row->expr.elems[1]->kind==ATOM_VAR && row->expr.elems[2]->kind==ATOM_VAR);
            CHECK(var_id_eq(row->expr.elems[1]->var_id,row->expr.elems[2]->var_id));
        }
        cetta_space_agenda_free(a);space_free(&space);
    }
    puts("PASS: predicate queries preserve fresh stored-variable aliases and apply only actual match refinements");
}

static void predicate_match_paid_suffix(Arena *arena,Bindings *env) {
    Space space={0};space_init(&space);
    add(&space,arena,"Slot");add(&space,arena,"(Request Later Payload)");
    add(&space,arena,"(exec Server (I (take Slot) (take (Request $id $payload)) "
        "(all (exec Server $i $o) ((exec Server $input $output)))) "
        "(O (+ (Reply $id $payload)) (+ (exec Server $input $output))))");
    CettaSpaceAgenda *a=cetta_space_agenda_new(&space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});CHECK(a);
    CettaSpaceAgendaMatch *match=NULL;
    for(unsigned i=0u;!match;i++) {
        CHECK(i<10000u && cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
        if(a->join_ready && a->depth<a->input_count &&
           a->inputs[a->depth].kind==INPUT_ALL &&
           shaped(a->levels[a->depth].pending_row,a->exec,3u) &&
           atom_is_symbol_id(a->levels[a->depth].pending_row->expr.elems[1],
               symbol_intern_cstr(g_symbols,"Server")))
            match=cetta_space_agenda_match_capture(a);
    }
    CettaNativeCostLedger original,suffix;
    cetta_native_cost_init_compact(&original);cetta_native_cost_init_compact(&suffix);
    CettaNativeCostScope scope;
    cetta_native_cost_scope_enter(&scope,&original,1u);
    CHECK(cetta_space_agenda_match_run(match,1u));
    cetta_native_cost_scope_leave(&scope);
    CHECK(match->decision==CETTA_MATCH_CURSOR_PROGRESS);
    CettaMatchCursor *copy=match_atoms_cursor_clone(match->cursor);CHECK(copy);
    CHECK(match_atoms_cursor_epoch(copy).word==match->epoch.word);
    uint64_t prefix[sizeof(original.total.counts)/sizeof(*original.total.counts)];
    memcpy(prefix,original.total.counts,sizeof(prefix));
    uint64_t prefix_work=original.total.work;
    for(unsigned i=0u;match->decision==CETTA_MATCH_CURSOR_PROGRESS;i++) {
        CHECK(i<10000u);
        cetta_native_cost_scope_enter(&scope,&original,1u);
        uint64_t before=original.total.work;
        CHECK(!cetta_space_agenda_match_run(match,0u));
        CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
        CHECK(original.total.work==before && a->matching==match);
        CHECK(cetta_space_agenda_match_run(match,CETTA_SPACE_AGENDA_MATCH_NODES));
        cetta_native_cost_scope_leave(&scope);
        HashConsTable *previous=g_hashcons;
        if(match->isolated)g_hashcons=&match->hashcons;
        cetta_native_cost_scope_enter(&scope,&suffix,1u);
        uint64_t held=suffix.total.work;
        CHECK(match_atoms_cursor_step(copy,0u)==CETTA_MATCH_CURSOR_PROGRESS);
        CHECK(suffix.total.work==held);
        CettaMatchCursorStep decision=match_atoms_cursor_step(copy,CETTA_SPACE_AGENDA_MATCH_NODES);
        cetta_native_cost_scope_leave(&scope);g_hashcons=previous;
        CHECK(decision==match->decision);
        CHECK(original.total.work==prefix_work+suffix.total.work);
        for(size_t k=0u;k<sizeof(prefix)/sizeof(*prefix);k++)
            CHECK(original.total.counts[k]==prefix[k]+suffix.total.counts[k]);
    }
    CHECK(match->decision==CETTA_MATCH_CURSOR_MATCHED);
    match_atoms_cursor_free(copy);
    uint64_t nodes=original.total.counts[CETTA_COST_MATCH_NODE];
    uint64_t attempts=original.total.counts[CETTA_COST_MATCH_ATTEMPT];
    cetta_native_cost_scope_enter(&scope,&original,1u);
    CHECK(cetta_space_agenda_step(a)==CETTA_SPACE_AGENDA_PROGRESS);
    CHECK(original.total.counts[CETTA_COST_MATCH_NODE]==nodes);
    CHECK(original.total.counts[CETTA_COST_MATCH_ATTEMPT]==attempts);
    cetta_native_cost_scope_leave(&scope);
    cetta_native_cost_free(&original);cetta_native_cost_free(&suffix);
    cetta_space_agenda_free(a);space_free(&space);
    puts("PASS: a retained server predicate matcher conserves every actual counter across its exact suffix; paid installation repeats no matcher work");
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
    /* The selected rule is consumed; its code is a pre-state query. */
    add(&space,arena,"(exec Server (I (take Slot) (take (Request $id $payload)) "
        "(all (exec Server $i $o) ((exec Server $input $output)))) "
        "(O (+ (Reply $id $payload)) (+ (exec Server $input $output))))");
    add(&space,arena,"(exec Consumer (I (take (Reply $id $payload)) (take Acknowledge) "
        "(all (exec Consumer $i $o) ((exec Consumer $input $output)))) "
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
        "(I (absent Stop))","(I (all (exec $k $i $o) ((exec $key $in $out))))"};
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
        "(I (absent Stop))","(I (all (exec $k $i $o) ((exec $key $in $out))))"};
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

/* Migration diagnostics use the same parser and checked matcher as the
 * provider. They inspect syntax without activating a source or its effects. */
static CettaMatchCompletion lint_overlap(Atom *left,Atom *right,Arena *arena,
        const Bindings *env,bool fresh_right) {
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch=cetta_frame_identity_scope_fresh(&frames);
    if(!epoch.word)return CETTA_MATCH_INCOMPLETE;
    /* Two input patterns share one source context. Materialize its left
     * view into the same epoch the checked matcher assigns to the right.
     * A stored exec candidate instead has an independent right context. */
    if(!fresh_right)left=rename_vars(arena,left,epoch);
    if(!left)return CETTA_MATCH_INCOMPLETE;
    BindingsBuilder builder;
    if(!bindings_builder_init(&builder,env))return CETTA_MATCH_INCOMPLETE;
    CettaMatchCompletion result=match_atoms_epoch_builder_checked(left,right,
        &builder,arena,epoch,UINT64_MAX);
    bindings_builder_free(&builder);return result;
}

static void lint_diagnostic(FILE *out,const char *path,size_t form,Atom *exec,
        const char *message,size_t read,size_t take) {
    if(!out)return;
    fprintf(out,"%s:form %zu: exec ",path,form);atom_print(exec->expr.elems[1],out);
    fprintf(out,": %s (input %zu, take %zu; take 0 is the selected exec)\n",
        message,read,take);
}

static size_t lint_transaction_exec(Atom *term,Space *space,Arena *arena,
        const Bindings *env,FILE *out,const char *path,size_t form,bool *complete) {
    if(!shaped(term,symbol_intern_cstr(g_symbols,"exec"),3u) ||
       !headed(term->expr.elems[2],symbol_intern_cstr(g_symbols,"I")))return 0u;
    CettaSpaceAgenda *a=cetta_space_agenda_new(space,arena,env,
        (CettaSpaceAgendaContract){.explicit_inputs=true});
    if(!a){*complete=false;return 0u;}
    a->selected=term;
    if(!parse_selected(a)) {
        lint_diagnostic(out,path,form,term,"uninterpreted Transaction source",0u,0u);
        *complete=false;cetta_space_agenda_free(a);return 0u;
    }
    size_t warnings=0u;
    for(size_t i=0u;i<a->input_count;i++) {
        if(a->inputs[i].kind!=INPUT_READ)continue;
        for(size_t j=0u;j<=a->input_count;j++) {
            if(j && a->inputs[j-1u].kind!=INPUT_TAKE)continue;
            Atom *taken=j ? a->inputs[j-1u].pattern : term;
            CettaMatchCompletion overlap=lint_overlap(a->inputs[i].pattern,taken,
                arena,env,!j);
            if(overlap==CETTA_MATCH_REFUTED)continue;
            if(overlap==CETTA_MATCH_INCOMPLETE)*complete=false;
            warnings++;
            lint_diagnostic(out,path,form,term,overlap==CETTA_MATCH_MATCHED ?
                "kept/taken patterns overlap: review distinct-occurrence intent, remove a redundant read, or use a pre-state predicate query" :
                "kept/taken overlap is undecided",i+1u,j);
        }
    }
    for(CettaExprIndex i=1u;i<a->sinks->expr.len;i++) {
        if(shaped(a->sinks->expr.elems[i],a->minus,1u)) {
            warnings++;
            lint_diagnostic(out,path,form,term,
                "legacy removal sink needs an explicit consumption-footprint review",0u,i);
        }
    }
    cetta_space_agenda_free(a);return warnings;
}

static size_t lint_transaction_tree(Atom *root,Space *space,Arena *arena,
        const Bindings *env,FILE *out,const char *path,size_t form,bool *complete) {
    size_t capacity=16u,length=0u,warnings=0u;
    Atom **pending=malloc(capacity*sizeof(*pending));
    if(!pending){*complete=false;return 0u;}
    pending[length++]=root;
    while(length) {
        Atom *term=pending[--length];
        warnings+=lint_transaction_exec(term,space,arena,env,out,path,form,complete);
        if(!term || term->kind!=ATOM_EXPR)continue;
        if(term->expr.len>SIZE_MAX-length){*complete=false;break;}
        size_t needed=length+(size_t)term->expr.len;
        if(needed>capacity) {
            if(needed>SIZE_MAX/sizeof(*pending)){*complete=false;break;}
            Atom **grown=realloc(pending,needed*sizeof(*pending));
            if(!grown){*complete=false;break;}
            pending=grown;capacity=needed;
        }
        for(CettaExprIndex i=term->expr.len;i>0u;i--)pending[length++]=term->expr.elems[i-1u];
    }
    free(pending);return warnings;
}

static void transaction_lint_controls(Arena *arena,Bindings *env) {
    const struct {const char *text;size_t warnings;} examples[]={
        {"(exec D1 (I (read Token) (take Token)) (O (+ Done)))",1u},
        {"(exec Reverse (I (take Token) (read Token)) (O (+ Done)))",1u},
        {"(exec Reserve (I (read (tok $x)) (take (tok $y))) (O (+ (took $y))))",1u},
        {"(exec Separate (I (read Fact) (take Token)) (O (+ Done)))",0u},
        {"(exec Aliases (I (read (p $x A)) (take (p B $x))) (O (+ Done)))",0u},
        {"(exec Query (I (all Token $xs) (take Token)) (O (+ Done)))",0u},
        {"(exec Self (I (read (exec Self $i $o))) (O (+ Done)))",1u},
        {"(exec Remove (I (read Token)) (O (- Token)))",1u}};
    Space space={0};space_init(&space);
    SpaceReadToken before=space_read_token(&space);
    for(size_t i=0u;i<sizeof(examples)/sizeof(*examples);i++) {
        bool complete=true;
        CHECK(lint_transaction_tree(parse(arena,examples[i].text),&space,arena,env,
            NULL,"control",i+1u,&complete)==examples[i].warnings);
        CHECK(complete && space_read_token_is_current(before));
    }
    space_free(&space);
    puts("PASS: Transaction lint uses the native matcher, preserves shared variable constraints and flags self-consumption without executing sources");
}

static void absent_read_watch_controls(void) {
    SpaceOccurrenceCursor cursor={0};
    SpaceCheckpointBudget clean={.work_limit=100u,.byte_limit=SIZE_MAX};
    CHECK(agenda_checkpoint_cursor_empty(&cursor,&clean));
    _Atomic uint64_t stamp=1u;SpaceReadWatch watch={&stamp,1u};
    for(unsigned field=0u;field<3u;field++) {
        cursor=(SpaceOccurrenceCursor){0};
        if(field==0u)cursor.logical_reads.root_watch.stamp=&stamp;
        if(field==1u)cursor.logical_reads.root_watch.expected=1u;
        if(field==2u)cursor.logical_reads.base_watches=&watch;
        SpaceCheckpointBudget budget={.work_limit=100u,.byte_limit=SIZE_MAX};
        CHECK(!agenda_checkpoint_cursor_empty(&cursor,&budget));
        CHECK(budget.refusal && !strcmp(budget.refusal,"AgendaInactiveSelection"));
    }
    puts("PASS: an inactive cursor cannot hide a retained read-watch field");
}

static int lint_transaction_files(int argc,char **argv,Bindings *env) {
    if(argc<3 || strcmp(argv[1],"--lint-transaction-reads")) {
        fputs("usage: test-native-space-agenda --lint-transaction-reads FILE...\n",stderr);
        return 2;
    }
    size_t warnings=0u;bool complete=true;
    for(int file=2;file<argc;file++) {
        Arena arena;arena_init(&arena);Space space={0};space_init(&space);
        Atom **forms=NULL;int count=parse_metta_file(argv[file],&arena,&forms);
        if(count<0){fprintf(stderr,"%s: parse failed\n",argv[file]);complete=false;}
        for(int i=0;i<count;i++)warnings+=lint_transaction_tree(forms[i],&space,&arena,
            env,stdout,argv[file],(size_t)i+1u,&complete);
        free(forms);space_free(&space);arena_free(&arena);
    }
    printf("Transaction read lint: %zu review findings; analysis %s\n",warnings,
        complete?"complete":"incomplete");
    return complete ? (warnings?1:0) : 2;
}

static void kept_removal_discovery_controls(Arena *arena,Bindings *env) {
    const CettaSpaceAgendaChoiceKind kinds[]={CETTA_SPACE_CHOICE_TRANSACTION_COMMIT,
        CETTA_SPACE_CHOICE_TRANSACTION_MATCH,CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH};
    for(size_t kind=0u;kind<3u;kind++) {
        Space space={0};space_init(&space);
        add(&space,arena,"(token A)");add(&space,arena,"(token B)");add(&space,arena,"(token B)");
        add(&space,arena,"(exec R (I (read (token $x))) (O (- (token $x)) (+ (Kept $x))))");
        SpaceReadToken before=space_read_token(&space);
        CettaSpaceAgendaDiscovery *discovery=cetta_space_agenda_discovery_new_owned_kind(&space,env,kinds[kind]);
        CettaSpaceAgendaChoice choices[4];CHECK(discovery);
        size_t count=discover_choices(discovery,choices,4u);CHECK(count==(kind?2u:1u));
        CHECK(space_read_token_is_current(before) && space_length64(&space)==4u);
        CettaSpaceAgendaSelection *selection=cetta_space_agenda_discovery_take(discovery,choices[0].ticket);
        CHECK(selection);cetta_space_agenda_discovery_free(discovery);
        if(kind==2u) {
            selected_world_commit(selection);
            CHECK(space_read_token_is_current(before) && space_length64(&space)==4u);
        } else {
            bool committed=false;
            for(unsigned i=0u;i<10000u;i++) {
                CettaSpaceAgendaStep step=cetta_space_agenda_selection_step(selection);
                if(step==CETTA_SPACE_AGENDA_COMMITTED){committed=true;break;}
                CHECK(step==CETTA_SPACE_AGENDA_PROGRESS || step==CETTA_SPACE_AGENDA_RETRY);
            }
            CHECK(committed);
        }
        Space *result=cetta_space_agenda_selection_provider(selection)->space;
        CHECK(space_length64(result)==3u && count_exact(result,arena,"(token B)")==1u &&
            space_contains_exact(result,parse(arena,"(Kept B)")));
        cetta_space_agenda_selection_free(selection);space_free(&space);
    }
}

int main(int argc,char **argv) {
    SymbolTable symbols;VarInternTable vars;Arena arena;Bindings env;
    symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    var_intern_init(&vars);g_var_intern=&vars;g_hashcons=NULL;arena_init(&arena);bindings_init(&env);
    if(argc==2 && !strcmp(argv[1],"--cost-index-oracle")) {
        set_capture_allocation_refusal(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        printf("Physical index-visit oracle: %zu checks passed\n",checks);
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--sink-order")) {
        int result=sink_order_controls(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return result;
    }
    if(argc==2 && !strcmp(argv[1],"--set-capture")) {
        int result=set_capture_occurrence_controls(&arena,&env);
        if(!result) {
            set_capture_allocation_refusal(&arena,&env);
            set_capture_admission_refusal(&arena,&env);
            set_capture_appended_duplicate(&arena,&env);
            set_capture_search_quanta(&arena,&env);
            set_capture_changed_source(&arena,&env);
            set_capture_retirement_steps(&arena,&env);
            set_capture_selected_equivalence(&arena,&env);
            set_capture_stored_data_scope(&arena,&env);
        }
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return result;
    }
    if(argc==2 && !strcmp(argv[1],"--set-captured")) {
        int result=set_capture_bound_environment(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return result;
    }
    if(argc==2 && !strcmp(argv[1],"--raw-captured")) {
        raw_capture_bound_inspect(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--kept-removal")) {
        removal_search_controls(&arena);
        kept_removal_preparation_controls(&arena,&env);
        kept_removal_validation_controls(&arena,&env);
        kept_removal_interruption_controls(&arena,&env);
        kept_removal_discovery_controls(&arena,&env);
        bool result=kept_read_legacy_removal(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        printf("Kept legacy removal: %zu checks, %s\n",checks,result?"pass":"FAIL");
        return result?0:1;
    }
    if(argc==2 && !strcmp(argv[1],"--legacy-policy")) {
        int result=legacy_removal_resolution_controls(&arena);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return result;
    }
    if(argc==2 && !strcmp(argv[1],"--sink-refusal")) {
        legacy_removal_refusal_controls(&arena,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        printf("Legacy sink refusal: %zu checks passed\n",checks);
        return 0;
    }
    if(argc>1) {
        int result=lint_transaction_files(argc,argv,&env);
        bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
        return result;
    }
    transaction_lint_controls(&arena,&env);
    absent_read_watch_controls();
    scoped_alpha(&arena);captured_environment(&arena,&env);alpha_set_members(&arena,&env);
    mm2(&arena,&env);self_and_new_exec(&arena,&env);self_reintroduced(&arena,&env);transaction(&arena,&env);
    controlled_mm2_activation(&arena,&env);
    disabled_then_enabled(&arena,&env);shared_reads_and_takes(&arena,&env);
    CHECK(set_capture_occurrence_controls(&arena,&env)==0);
    set_capture_allocation_refusal(&arena,&env);
    set_capture_admission_refusal(&arena,&env);
    set_capture_appended_duplicate(&arena,&env);
    set_capture_search_quanta(&arena,&env);
    set_capture_changed_source(&arena,&env);
    set_capture_retirement_steps(&arena,&env);
    set_capture_selected_equivalence(&arena,&env);
    set_capture_stored_data_scope(&arena,&env);
    CHECK(legacy_removal_resolution_controls(&arena)==0);
    legacy_removal_refusal_controls(&arena,&env);
    removal_search_controls(&arena);
    kept_removal_preparation_controls(&arena,&env);
    kept_removal_validation_controls(&arena,&env);
    kept_removal_interruption_controls(&arena,&env);
    kept_removal_discovery_controls(&arena,&env);
    kept_read_identification(&arena,&env);
    predicate_query_context(&arena,&env);
    predicate_match_paid_suffix(&arena,&env);
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
    bool bound_scope_ok=set_capture_bound_environment(&arena,&env)==0;
    bool legacy_kept_ok=kept_read_legacy_removal(&arena,&env);
    CHECK(bound_scope_ok && legacy_kept_ok);
    bindings_free(&env);arena_free(&arena);var_intern_free(&vars);symbol_table_free(&symbols);
    puts("PASS: native space agenda joins, restores self reads, creates execs and consumes physical bag occurrences");
    printf("%zu checks passed\n",checks);return 0;
}
