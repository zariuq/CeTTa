#include "search_machine.h"
#include "native_cost.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static size_t checks, destroyed;
#define CHECK(X) do {assert(X); checks++;} while(0)
static CettaContinuationStatus capture(void *m, void **p) {(void)m;(void)p;return CETTA_CONTINUATION_UNSUPPORTED;}
static CettaContinuationStatus restore(void *m, void **p) {(void)m;(void)p;return CETTA_CONTINUATION_UNSUPPORTED;}
static void destroy(void *p) {destroyed++;free(p);}
static bool storage(const void *p,CettaContinuationStorage *s) {(void)p;*s=(CettaContinuationStorage){.exclusive_bytes=sizeof(unsigned)};return true;}
static const CettaContinuationProvider provider={.representation_name="lease-control",.ownership={capture,restore,destroy,storage}};
static CettaOwnedContinuation occurrence(unsigned n) {unsigned *p=malloc(sizeof(*p));*p=n;return (CettaOwnedContinuation){.payload=p,.provider=&provider};}
static void init(CettaContinuationHub *h) {CettaSelectionAutomaton a;CettaControlPlan p;CHECK(cetta_selection_automaton_fifo(&a));CHECK(cetta_control_plan_derive((CettaObservationDemand){.completion=CETTA_OBSERVATION_ORDERED_STREAM},CETTA_CONTROL_BRANCH_GENERAL,CETTA_CONTROL_BATCH_SINGLETON_ONLY,&p));CHECK(cetta_continuation_hub_init(h,&a,CETTA_SELECTION_DUTY_RECURRENT_OLDEST,&p));}
static void test_leases(void) {
 CettaContinuationHub a={0},b={0};init(&a);init(&b);
 CettaOwnedContinuation x=occurrence(11),y=occurrence(22),z=occurrence(33),held={0};
 CHECK(cetta_continuation_hub_append(&a,&x));CHECK(cetta_continuation_hub_append(&a,&y));CHECK(cetta_continuation_hub_append(&b,&z));
 CHECK(a.store.identity && a.store.identity!=b.store.identity);
 const void *first=cetta_continuation_hub_at(&a,0)->payload;
 CHECK(!cetta_continuation_hub_take_for_resume(&a,2,&held));CHECK(cetta_continuation_hub_at(&a,0)->payload==first);
 CHECK(cetta_continuation_hub_take_for_resume(&a,0,&held));CHECK(held.occurrence_id==1 && held.resume_store==a.store.identity && held.resume_lease);
 CHECK(!cetta_continuation_hub_take_for_resume(&a,0,&held));CHECK(cetta_continuation_hub_length(&a)==1);
 CettaOwnedContinuation copied=held;uint64_t lease=held.resume_lease;
 CHECK(!cetta_continuation_hub_requeue(&b,&held));CHECK(held.payload==first && held.resume_lease==lease && a.store.lease_length==1);
 uint64_t later=a.store.items[a.store.begin].occurrence_id;
 a.store.items[a.store.begin].occurrence_id=held.occurrence_id;
 CHECK(!cetta_continuation_hub_requeue(&a,&held));CHECK(held.resume_lease==lease && a.store.lease_length==1);
 a.store.items[a.store.begin].occurrence_id=later;
 CHECK(cetta_continuation_hub_requeue(&a,&held));CHECK(!held.payload && a.store.lease_length==0);
 CHECK(cetta_continuation_hub_at(&a,1)->payload==first && cetta_continuation_hub_at(&a,1)->occurrence_id==1);
 CHECK(!cetta_continuation_hub_requeue(&a,&copied));CHECK(copied.payload==first);
 CHECK(cetta_continuation_hub_take_for_resume(&a,1,&held));CHECK(held.resume_lease!=lease);
 CHECK(!cetta_continuation_hub_release_lease(&a,&copied));CHECK(cetta_continuation_hub_release_lease(&a,&held));
 CHECK(!cetta_continuation_hub_requeue(&a,&held));cetta_owned_continuation_destroy(&held);
 CHECK(destroyed==1);
 a.store.next_lease=UINT64_MAX;
 CHECK(cetta_continuation_hub_take_for_resume(&a,0,&held));CHECK(held.resume_lease==UINT64_MAX && a.store.next_lease==0);
 CHECK(cetta_continuation_hub_requeue(&a,&held));first=cetta_continuation_hub_at(&a,0)->payload;
 CHECK(!cetta_continuation_hub_take_for_resume(&a,0,&held));CHECK(cetta_continuation_hub_at(&a,0)->payload==first);
 CHECK(cetta_continuation_hub_take_for_resume(&b,0,&held));uint64_t previous=b.store.identity;
 cetta_continuation_hub_destroy(&b);CHECK(!cetta_continuation_hub_requeue(&b,&held));init(&b);
 CHECK(b.store.identity!=previous && !cetta_continuation_hub_requeue(&b,&held));cetta_owned_continuation_destroy(&held);
 cetta_continuation_hub_destroy(&a);cetta_continuation_hub_destroy(&b);CHECK(destroyed==3);
 printf("Owned continuation leases: %zu checks passed\n",checks);
}

typedef struct {
    CettaContinuationHub hub;
    CettaOwnedContinuation queued[8];
    CettaContinuationLease leases[4];
} HubBefore;

static void save_hub(const CettaContinuationHub *hub, HubBefore *before) {
    CHECK(hub->store.end - hub->store.begin <= 8u);
    CHECK(hub->store.lease_length <= 4u);
    memcpy(&before->hub, hub, sizeof(*hub));
    for (size_t i = hub->store.begin; i < hub->store.end; i++)
        memcpy(&before->queued[i - hub->store.begin], &hub->store.items[i],
               sizeof(*hub->store.items));
    if (hub->store.lease_length)
        memcpy(before->leases, hub->store.leases,
               hub->store.lease_length * sizeof(*hub->store.leases));
}

static void check_hub_unchanged(const CettaContinuationHub *hub,
                                const HubBefore *before) {
    CHECK(memcmp(hub, &before->hub, sizeof(*hub)) == 0);
    for (size_t i = hub->store.begin; i < hub->store.end; i++)
        CHECK(memcmp(&hub->store.items[i],
              &before->queued[i - hub->store.begin],
              sizeof(*hub->store.items)) == 0);
    if (hub->store.lease_length)
        CHECK(memcmp(hub->store.leases, before->leases,
              hub->store.lease_length * sizeof(*hub->store.leases)) == 0);
}

static void check_prepared_refusal(const CettaContinuationHub *source,
        CettaContinuationTransport *transport, size_t count,
        CettaContinuationHub *destination, CettaContinuationStatus expected) {
    CHECK(count <= 4u);
    HubBefore before, destination_before;
    save_hub(source, &before);
    save_hub(destination, &destination_before);
    CettaContinuationTransport saved[4];
    if (count)
        memcpy(saved, transport, count * sizeof(*transport));
    size_t old_destroyed = destroyed;
    CHECK(cetta_continuation_hub_copy_prepared(
        source, transport, count, destination) == expected);
    check_hub_unchanged(source, &before);
    check_hub_unchanged(destination, &destination_before);
    if (count)
        CHECK(memcmp(saved, transport, count * sizeof(*transport)) == 0);
    CHECK(destroyed == old_destroyed);
}

typedef struct {
    uint64_t occurrence, parent;
    unsigned value;
    uint32_t phase;
} HubDelivery;

static size_t drain_hub(CettaContinuationHub *hub, HubDelivery *out) {
    size_t length = 0u;
    while (cetta_continuation_hub_length(hub)) {
        CettaSelectionLane lane;
        size_t index;
        CHECK(cetta_continuation_hub_select(hub, &lane, &index));
        CettaOwnedContinuation owned = {0};
        CHECK(cetta_continuation_hub_take(hub, index, &owned));
        out[length++] = (HubDelivery){owned.occurrence_id,
            owned.parent_occurrence_id, *(unsigned *)owned.payload,
            hub->schedule.state};
        cetta_owned_continuation_destroy(&owned);
    }
    return length;
}

static void test_prepared_hub_copy(void) {
    CettaContinuationHub source = {0}, copy = {0};
    CettaSelectionAutomaton schedule;
    CettaControlPlan plan;
    CHECK(cetta_selection_automaton_ratio(3u, &schedule));
    CHECK(cetta_control_plan_derive((CettaObservationDemand){
        .completion = CETTA_OBSERVATION_FINITE_PREFIX, .prefix_limit = 20u},
        CETTA_CONTROL_BRANCH_GENERAL, CETTA_CONTROL_BATCH_SINGLETON_ONLY,
        &plan));
    CHECK(cetta_continuation_hub_init(&source, &schedule,
        CETTA_SELECTION_DUTY_RECURRENT_OLDEST, &plan));
    for (unsigned i = 0u; i < 4u; i++) {
        CettaOwnedContinuation owned = occurrence(i < 2u ? 7u : 9u);
        owned.parent_occurrence_id = 100u + i;
        CHECK(cetta_continuation_hub_append(&source, &owned));
    }
    CettaSelectionLane lane;
    size_t index;
    CHECK(cetta_continuation_hub_select(&source, &lane, &index));
    CHECK(cetta_continuation_hub_select(&source, &lane, &index));
    CHECK(source.schedule.state == 2u);
    CHECK(!cetta_continuation_hub_observe(&source, 7u));
    CettaOwnedContinuation issued[2] = {{0}};
    CHECK(cetta_continuation_hub_take_for_resume(&source, 0u, &issued[0]));
    CHECK(cetta_continuation_hub_take_for_resume(&source, 1u, &issued[1]));
    CHECK(source.store.begin != 0u && source.store.lease_length == 2u);
    const CettaOwnedContinuation *inputs[] = {&issued[1],
        cetta_continuation_hub_at(&source, 1u), &issued[0],
        cetta_continuation_hub_at(&source, 0u)};
    CettaContinuationTransport transport[4];
    for (size_t i = 0u; i < 4u; i++)
        transport[i] = (CettaContinuationTransport){inputs[i],
            occurrence(*(unsigned *)inputs[i]->payload)};

    check_prepared_refusal(&source, transport, 3u, &copy,
                          CETTA_CONTINUATION_UNSUPPORTED);
    transport[1].source = inputs[0];
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_UNSUPPORTED);
    transport[1].source = inputs[1];
    CettaOwnedContinuation wrong = issued[1];
    wrong.resume_store++;
    transport[0].source = &wrong;
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_INVALIDATED);
    wrong = issued[1];
    wrong.payload = issued[0].payload;
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_INVALIDATED);
    wrong = issued[1];
    wrong.resume_lease = source.store.next_lease;
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_INVALIDATED);
    transport[0].source = inputs[0];
    transport[2].prepared.occurrence_id = issued[0].occurrence_id;
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_UNSUPPORTED);
    transport[2].prepared.occurrence_id = 0u;
    void *saved_payload = transport[2].prepared.payload;
    transport[2].prepared.payload = NULL;
    check_prepared_refusal(&source, transport, 4u, &copy,
                          CETTA_CONTINUATION_UNSUPPORTED);
    transport[2].prepared.payload = saved_payload;
    CettaContinuationHub occupied = {0};
    init(&occupied);
    CettaOwnedContinuation already_owned = occurrence(55u);
    CHECK(cetta_continuation_hub_append(&occupied, &already_owned));
    check_prepared_refusal(&source, transport, 4u, &occupied,
                          CETTA_CONTINUATION_UNSUPPORTED);
    cetta_continuation_hub_destroy(&occupied);

    HubBefore before;
    save_hub(&source, &before);
    CettaNativeCostLedger ledger;
    CettaNativeCostScope cost_scope;
    cetta_native_cost_init(&ledger);
    cetta_native_cost_scope_enter(&cost_scope, &ledger, 29u);
    CHECK(cetta_continuation_hub_copy_prepared(
        &source, transport, 4u, &copy) == CETTA_CONTINUATION_READY);
    cetta_native_cost_scope_leave(&cost_scope);
    CHECK(ledger.total.counts[CETTA_COST_QUANTUM] == 0u);
    CHECK(ledger.total.counts[CETTA_COST_CAPTURE] == 4u);
    CHECK(ledger.total.counts[CETTA_COST_FRAME_ALLOCATE] == 2u);
    CHECK(ledger.total.counts[CETTA_COST_QUEUE_VISIT] > 0u);
    cetta_native_cost_free(&ledger);
    check_hub_unchanged(&source, &before);
    CHECK(copy.store.identity && copy.store.identity != source.store.identity);
    CHECK(memcmp(&copy.schedule, &source.schedule, sizeof(copy.schedule)) == 0);
    CHECK(copy.selection_duty == source.selection_duty);
    CHECK(copy.plan.readout == source.plan.readout &&
          copy.plan.activation == source.plan.activation &&
          copy.plan.prefix_limit == source.plan.prefix_limit);
    CHECK(copy.observed_occurrences == 7u);
    CHECK(copy.store.begin == source.store.begin &&
          copy.store.end == source.store.end &&
          copy.store.capacity == source.store.capacity &&
          copy.store.lease_length == source.store.lease_length &&
          copy.store.lease_capacity == source.store.lease_capacity &&
          copy.store.next_occurrence_id == source.store.next_occurrence_id &&
          copy.store.next_lease == source.store.next_lease);
    CHECK(!transport[1].prepared.payload && !transport[3].prepared.payload);
    for (size_t i = 0u; i < 2u; i++) {
        size_t t = i == 0u ? 2u : 0u;
        CettaOwnedContinuation *prepared = &transport[t].prepared;
        CHECK(prepared->occurrence_id == issued[i].occurrence_id &&
              prepared->parent_occurrence_id == issued[i].parent_occurrence_id &&
              prepared->resume_store == copy.store.identity &&
              prepared->resume_lease == issued[i].resume_lease);
        CHECK(!cetta_continuation_hub_requeue(&copy, &issued[i]));
        CHECK(!cetta_continuation_hub_requeue(&source, prepared));
        CHECK(prepared->payload != issued[i].payload);
        unsigned value = *(unsigned *)issued[i].payload;
        *(unsigned *)issued[i].payload = 71u;
        CHECK(*(unsigned *)prepared->payload == value);
        *(unsigned *)issued[i].payload = value;
        CHECK(cetta_continuation_hub_requeue(&source, &issued[i]));
        CHECK(cetta_continuation_hub_requeue(&copy, prepared));
    }
    CHECK(!cetta_continuation_hub_observe(&source, 3u));
    CHECK(!cetta_continuation_hub_observe(&copy, 3u));
    CHECK(source.observed_occurrences == copy.observed_occurrences);
    HubDelivery expected[4], actual[4];
    size_t expected_length = drain_hub(&source, expected);
    cetta_continuation_hub_destroy(&source);
    size_t actual_length = drain_hub(&copy, actual);
    CHECK(actual_length == 4u && actual_length == expected_length);
    for (size_t i = 0u; i < actual_length; i++) {
        CHECK(actual[i].occurrence == expected[i].occurrence &&
              actual[i].parent == expected[i].parent &&
              actual[i].value == expected[i].value &&
              actual[i].phase == expected[i].phase);
    }
    cetta_continuation_hub_destroy(&copy);
    puts("PASS: prepared hub copy preserves queued and issued occurrences, phase, demand and exact future delivery");
}

typedef struct { unsigned value, references; } SharedLeasePayload;
static size_t shared_destroyed;
static void shared_destroy(void *raw) {
    SharedLeasePayload *payload = raw;
    CHECK(payload->references > 0u);
    if (--payload->references == 0u) {
        shared_destroyed++;
        free(payload);
    }
}
static const CettaContinuationProvider shared_provider = {
    .representation_name = "shared-immutable-lease-control",
    .ownership = {capture, restore, shared_destroy, storage},
};

static void test_shared_physical_hub_copy(void) {
    CettaContinuationHub source = {0}, copy = {0};
    init(&source);
    SharedLeasePayload *old = malloc(sizeof(*old)), *fresh = malloc(sizeof(*fresh));
    CHECK(old && fresh);
    *old = (SharedLeasePayload){17u, 2u};
    *fresh = *old;
    for (size_t i = 0u; i < 2u; i++) {
        CettaOwnedContinuation item = {.payload = old, .provider = &shared_provider};
        CHECK(cetta_continuation_hub_append(&source, &item));
    }
    CettaOwnedContinuation held = {0};
    CHECK(cetta_continuation_hub_take_for_resume(&source, 0u, &held));
    CettaContinuationTransport transport[] = {
        {cetta_continuation_hub_at(&source, 0u),
         {.payload = fresh, .provider = &shared_provider}},
        {&held, {.payload = fresh, .provider = &shared_provider}},
    };
    CHECK(cetta_continuation_hub_copy_prepared(&source, transport, 2u, &copy)
          == CETTA_CONTINUATION_READY);
    CHECK(cetta_continuation_hub_at(&copy, 0u)->payload ==
          transport[1].prepared.payload);
    CHECK(cetta_continuation_hub_at(&copy, 0u)->occurrence_id !=
          transport[1].prepared.occurrence_id);
    CHECK(cetta_continuation_hub_release_lease(&source, &held));
    cetta_owned_continuation_destroy(&held);
    cetta_continuation_hub_destroy(&source);
    CHECK(shared_destroyed == 1u && fresh->references == 2u);
    CHECK(cetta_continuation_hub_requeue(&copy, &transport[1].prepared));
    cetta_continuation_hub_destroy(&copy);
    CHECK(shared_destroyed == 2u);
    puts("PASS: shared physical payloads remain distinct owned logical occurrences after copy");
}

static void test_hub_copy_empty_and_exhausted(void) {
    CettaContinuationHub source = {0}, copy = {0};
    init(&source);
    CHECK(cetta_continuation_hub_copy_prepared(&source, NULL, 0u, &copy)
          == CETTA_CONTINUATION_READY);
    CHECK(copy.store.identity != source.store.identity && copy.store.next_lease == 1u);
    CHECK(copy.store.next_occurrence_id == 1u && !copy.store.items && !copy.store.leases);
    cetta_continuation_hub_destroy(&copy);
    source.store.next_occurrence_id = UINT64_MAX;
    source.store.next_lease = UINT64_MAX;
    CettaOwnedContinuation value = occurrence(31u), held = {0};
    CHECK(cetta_continuation_hub_append(&source, &value));
    CHECK(cetta_continuation_hub_take_for_resume(&source, 0u, &held));
    CettaContinuationTransport transport = {&held, occurrence(31u)};
    CHECK(cetta_continuation_hub_copy_prepared(&source, &transport, 1u, &copy)
          == CETTA_CONTINUATION_READY);
    CHECK(!copy.store.next_occurrence_id && !copy.store.next_lease);
    CHECK(transport.prepared.occurrence_id == UINT64_MAX &&
          transport.prepared.resume_lease == UINT64_MAX);
    CettaOwnedContinuation additional = occurrence(31u), attempt = {0};
    CHECK(!cetta_continuation_hub_append(&copy, &additional));
    CHECK(additional.payload != NULL);
    cetta_owned_continuation_destroy(&additional);
    CHECK(cetta_continuation_hub_requeue(&copy, &transport.prepared));
    CHECK(!cetta_continuation_hub_take_for_resume(&copy, 0u, &attempt));
    CHECK(cetta_continuation_hub_length(&copy) == 1u);
    CHECK(cetta_continuation_hub_release_lease(&source, &held));
    cetta_owned_continuation_destroy(&held);
    cetta_continuation_hub_destroy(&source);
    cetta_continuation_hub_destroy(&copy);
    puts("PASS: empty and fully issued hub copies preserve exhausted occurrence and lease counters");
}

int main(void) {
    test_leases();
    test_prepared_hub_copy();
    test_shared_physical_hub_copy();
    test_hub_copy_empty_and_exhausted();
    printf("Owned hub capture and leases: %zu checks passed\n", checks);
    return 0;
}
