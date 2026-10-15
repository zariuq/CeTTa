/* Synchronous witnesses at selected controller transitions. These are
 * authored from successful take, first delivery and positive spent work.
 * The cost observer is a separate service; a missing charge cannot supply
 * its own required witness. No borrowed scope survives the callback. */
typedef struct {
    CettaNativeCostKind kind;
    uint64_t trigger, origin, occurrence;
    size_t command, first_destination, destination_count;
} PrimeReplayPhase;
typedef struct {
    PrimeReplayPhase *items;
    CettaNativeCostDestinationReceipt *destinations;
    size_t count, capacity, destination_count, destination_capacity;
    const CettaNativeCostReceipts *commands;
    bool incomplete;
} PrimeReplayPhases;
static _Thread_local PrimeReplayPhases *g_prime_replay_phases;

static bool prime_replay_phases_init(PrimeReplayPhases *out,size_t capacity,
    size_t destinations,const CettaNativeCostReceipts *commands) {
    if(capacity>SIZE_MAX/sizeof(*out->items)||
        destinations>SIZE_MAX/sizeof(*out->destinations))return false;
    *out=(PrimeReplayPhases){.capacity=capacity,.destination_capacity=destinations,
        .commands=commands};
    out->items=capacity?calloc(capacity,sizeof(*out->items)):NULL;
    out->destinations=destinations?calloc(destinations,sizeof(*out->destinations)):NULL;
    if((capacity&&!out->items)||(destinations&&!out->destinations)) {
        out->incomplete=true;return false;
    }
    return true;
}
static void prime_replay_phases_free(PrimeReplayPhases *out) {
    free(out->items);free(out->destinations);*out=(PrimeReplayPhases){0};
}
static void prime_replay_phase_required(CettaNativeCostKind kind,uint64_t trigger) {
    PrimeReplayPhases *out=g_prime_replay_phases;
    if(!out||out->incomplete)return;
    const CettaNativeCostScope *head=cetta_native_cost_current_scope;
    if(!head||!out->commands||out->commands->open||out->count==out->capacity) {
        out->incomplete=true;return;
    }
    PrimeReplayPhase *row=&out->items[out->count++];
    *row=(PrimeReplayPhase){.kind=kind,.trigger=trigger,
        .origin=head->ledger?head->ledger->identity:0u,
        .occurrence=cetta_native_cost_scope_occurrence(head),
        .command=out->commands->command_count,.first_destination=out->destination_count};
    for(const CettaNativeCostScope *p=head;p;p=p->previous) {
        if(out->destination_count==out->destination_capacity) {
            out->incomplete=true;return;
        }
        bool duplicate=false;
        for(const CettaNativeCostScope *q=head;q!=p;q=q->previous)
            if(q->ledger==p->ledger)duplicate=true;
        CettaNativeCostSnapshot before=p->ledger?p->ledger->total:(CettaNativeCostSnapshot){0};
        /* Later duplicate visits see the first visit's already-paid state.
         * The full serial profile refuses overflow or retention failure. */
        CettaNativeCostSnapshot after=before;
        if(p->ledger) {
            if(before.overflow||before.trace_incomplete||
                before.work==UINT64_MAX||before.span==UINT64_MAX||
                before.counts[kind]==UINT64_MAX) {out->incomplete=true;return;}
            after.work++;after.span++;after.counts[kind]++;
        }
        out->destinations[out->destination_count++]=(CettaNativeCostDestinationReceipt){
            .ledger_identity=p->ledger?p->ledger->identity:0u,
            .scope_occurrence=cetta_native_cost_scope_occurrence(p),.present=p->ledger!=NULL,
            .duplicate=duplicate,.retained=p->ledger&&!duplicate,
            .before=duplicate?after:before,.after=after};
        row->destination_count++;
    }
}
static bool prime_replay_phase_kind(CettaNativeCostKind kind) {
    return kind==CETTA_COST_RESTORE||kind==CETTA_COST_PUBLISH||kind==CETTA_COST_QUANTUM;
}
static bool prime_replay_phase_snapshot(const CettaNativeCostSnapshot *a,
    const CettaNativeCostSnapshot *b) {
    for(size_t i=0u;i<CETTA_COST_KIND_COUNT;i++)if(a->counts[i]!=b->counts[i])return false;
    return a->work==b->work&&a->span==b->span&&a->parallel==b->parallel&&
        a->overflow==b->overflow&&a->trace_incomplete==b->trace_incomplete;
}
static bool prime_replay_phases_complete(const PrimeReplayPhases *phases,
    const CettaNativeCostReceipts *commands) {
    if(phases->incomplete||phases->commands!=commands||
        !cetta_native_cost_receipts_complete(commands)||
        phases->count>phases->capacity||phases->destination_count>phases->destination_capacity)
        return false;
    size_t at=0u;
    for(size_t i=0u;i<commands->command_count;i++) {
        const CettaNativeCostCommandReceipt *actual=&commands->commands[i];
        if(!prime_replay_phase_kind(actual->event.kind))continue;
        if(at==phases->count)return false;
        const PrimeReplayPhase *required=&phases->items[at++];
        if(required->command!=i||required->kind!=actual->event.kind||
            required->origin!=actual->event.origin||required->occurrence!=actual->event.occurrence||
            actual->event.units!=1u||actual->event.repetitions!=1u||
            !actual->event.identity||actual->event.identity==UINT64_MAX||!actual->scoped||
            required->destination_count!=actual->destination_count||
            required->first_destination>phases->destination_count||
            required->destination_count>phases->destination_count-required->first_destination||
            actual->first_destination>commands->destination_count||
            actual->destination_count>commands->destination_count-actual->first_destination)return false;
        if(required->kind==CETTA_COST_RESTORE&&
            (!required->trigger||required->trigger!=required->occurrence))return false;
        if(required->kind==CETTA_COST_QUANTUM&&(!required->trigger||required->occurrence))return false;
        for(size_t j=0u;j<required->destination_count;j++) {
            const CettaNativeCostDestinationReceipt *x=&phases->destinations[required->first_destination+j];
            const CettaNativeCostDestinationReceipt *y=&commands->destinations[actual->first_destination+j];
            if(x->ledger_identity!=y->ledger_identity||x->scope_occurrence!=y->scope_occurrence||
                x->present!=y->present||x->duplicate!=y->duplicate||x->retained!=y->retained||
                !prime_replay_phase_snapshot(&x->before,&y->before)||
                !prime_replay_phase_snapshot(&x->after,&y->after))return false;
        }
    }
    return at==phases->count;
}
